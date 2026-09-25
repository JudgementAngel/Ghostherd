// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "Tools/MCPAssetManagementTools.h"
#include "MCPToolRegistry.h"
#include "MCPToolBuilder.h"
#include "MCPProtocol.h"
#include "MCPValidate.h"

#include "Editor.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "HAL/FileManager.h"
#include "Engine/Texture2D.h"
#include "Engine/StaticMesh.h"
#include "Factories/FbxImportUI.h"
#include "Factories/FbxStaticMeshImportData.h"
#include "AutomatedAssetImportData.h"

namespace MCPAssetManagementTools
{

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// create_folder - Create content browser folder
	// ================================================================
	MCP_TOOL(Registry, "create_folder")
		.Description(TEXT("Create a new folder in the content browser. Creates all intermediate directories if needed. Does nothing if the folder already exists."))
		.Idempotent()
		.StringArg(TEXT("folder_path"), TEXT("Content folder path to create (e.g., '/Game/UI/Icons', '/Game/Materials/Environment')"), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString FolderPath;
			if (!Args->TryGetStringField(TEXT("folder_path"), FolderPath))
				return FMCPToolResult::Error(TEXT("folder_path is required"));

			// v4 Phase 0 (security fix): reject traversal ('..') and malformed paths
			// before mapping to the filesystem — previously '/Game/../../x' escaped
			// the Content directory.
			BAIL_IF_INVALID(FMCPValidate::PackagePath(FolderPath));

			// Convert content path to filesystem path
			FString DiskPath = FPackageName::LongPackageNameToFilename(FolderPath);

			if (FPaths::DirectoryExists(DiskPath))
			{
				return FMCPToolResult::Success(FString::Printf(TEXT("Folder already exists: %s"), *FolderPath));
			}

			IFileManager::Get().MakeDirectory(*DiskPath, true);

			if (FPaths::DirectoryExists(DiskPath))
			{
				return FMCPToolResult::Success(FString::Printf(TEXT("Created folder: %s"), *FolderPath));
			}

			return FMCPToolResult::Error(FString::Printf(TEXT("Failed to create folder: %s"), *FolderPath));
		});

	// ================================================================
	// move_assets_to_folder - Bulk move assets with reference fixup
	// ================================================================
	MCP_TOOL(Registry, "move_assets_to_folder")
		.Description(TEXT("Bulk move multiple assets to a destination folder with automatic reference fixup. Creates the destination folder if it doesn't exist."))
		.StringArrayArg(TEXT("asset_paths"), TEXT("Array of asset content paths to move"), true)
		.StringArg(TEXT("destination_folder"), TEXT("Destination content folder (e.g., '/Game/UI/Icons')"), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			const TArray<TSharedPtr<FJsonValue>>* PathsArray;
			if (!Args->TryGetArrayField(TEXT("asset_paths"), PathsArray) || PathsArray->Num() == 0)
				return FMCPToolResult::Error(TEXT("asset_paths array is required"));

			FString DestFolder;
			if (!Args->TryGetStringField(TEXT("destination_folder"), DestFolder))
				return FMCPToolResult::Error(TEXT("destination_folder is required"));
			BAIL_IF_INVALID(FMCPValidate::PackagePath(DestFolder));

			IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

			TArray<FAssetRenameData> RenameData;
			TArray<FString> Moved;
			TArray<FString> Failed;

			for (const TSharedPtr<FJsonValue>& PathVal : *PathsArray)
			{
				FString AssetPath = PathVal->AsString();
				UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath);
				if (!Asset)
				{
					Failed.Add(FString::Printf(TEXT("%s (not found)"), *AssetPath));
					continue;
				}

				FString AssetName = FPackageName::GetShortName(AssetPath);
				FAssetRenameData Data;
				Data.Asset = Asset;
				Data.NewPackagePath = DestFolder;
				Data.NewName = AssetName;
				RenameData.Add(Data);
				Moved.Add(AssetName);
			}

			if (RenameData.Num() > 0)
			{
				bool bSuccess = AssetTools.RenameAssets(RenameData);
				if (!bSuccess)
				{
					return FMCPToolResult::Error(TEXT("Rename/move operation failed. Check Output Log for details."));
				}
			}

			FString Result = FString::Printf(TEXT("Moved %d asset(s) to '%s'"), Moved.Num(), *DestFolder);
			if (Moved.Num() > 0)
				Result += FString::Printf(TEXT(": %s"), *FString::Join(Moved, TEXT(", ")));
			if (Failed.Num() > 0)
				Result += FString::Printf(TEXT(". Failed: %s"), *FString::Join(Failed, TEXT(", ")));

			return FMCPToolResult::Success(Result);
		});

	// ================================================================
	// get_asset_size_report - Report largest assets
	// ================================================================
	MCP_TOOL(Registry, "get_asset_size_report")
		.Description(TEXT("Report the largest assets in the project by disk size. Useful for finding optimization targets. Filter by class to focus on textures, meshes, etc."))
		.ReadOnly()
		.Idempotent()
		.StringArg(TEXT("path"), TEXT("Content path to scan (default: '/Game/')"))
		.StringArg(TEXT("class_filter"), TEXT("Filter by asset class (e.g., 'Texture2D', 'StaticMesh')"))
		.IntArg(TEXT("limit"), TEXT("Maximum results (default: 20)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString Path = TEXT("/Game/");
			Args->TryGetStringField(TEXT("path"), Path);

			FString ClassFilter;
			Args->TryGetStringField(TEXT("class_filter"), ClassFilter);

			int32 Limit = 20;
			if (Args->HasField(TEXT("limit")))
				Limit = FMath::Clamp((int32)Args->GetNumberField(TEXT("limit")), 1, 100);

			FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
			IAssetRegistry& AR = ARM.Get();

			TArray<FAssetData> AllAssets;
			AR.GetAssetsByPath(FName(*Path), AllAssets, true);

			// Build size data
			TArray<TPair<int64, FAssetData>> SizedAssets;

			for (const FAssetData& Asset : AllAssets)
			{
				if (!ClassFilter.IsEmpty())
				{
					FString ClassName = Asset.AssetClassPath.GetAssetName().ToString();
					if (!ClassName.Contains(ClassFilter))
						continue;
				}

				// Get file size from disk
				FString PackageFilename = FPackageName::LongPackageNameToFilename(
					Asset.PackageName.ToString(), FPackageName::GetAssetPackageExtension());
				int64 FileSize = IFileManager::Get().FileSize(*PackageFilename);
				if (FileSize > 0)
				{
					SizedAssets.Add(TPair<int64, FAssetData>(FileSize, Asset));
				}
			}

			// Sort by size descending
			SizedAssets.Sort([](const TPair<int64, FAssetData>& A, const TPair<int64, FAssetData>& B)
			{
				return A.Key > B.Key;
			});

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			TArray<TSharedPtr<FJsonValue>> AssetArray;

			int64 TotalSize = 0;
			int32 Count = FMath::Min(SizedAssets.Num(), Limit);
			for (int32 i = 0; i < Count; i++)
			{
				const auto& Entry = SizedAssets[i];
				TSharedPtr<FJsonObject> AObj = MakeShared<FJsonObject>();
				AObj->SetStringField(TEXT("name"), Entry.Value.AssetName.ToString());
				AObj->SetStringField(TEXT("path"), Entry.Value.GetObjectPathString());
				AObj->SetStringField(TEXT("class"), Entry.Value.AssetClassPath.GetAssetName().ToString());
				AObj->SetNumberField(TEXT("size_bytes"), (double)Entry.Key);
				AObj->SetStringField(TEXT("size_readable"),
					Entry.Key > 1048576 ? FString::Printf(TEXT("%.1f MB"), Entry.Key / 1048576.0)
					: FString::Printf(TEXT("%.1f KB"), Entry.Key / 1024.0));
				AssetArray.Add(MakeShared<FJsonValueObject>(AObj));
				TotalSize += Entry.Key;
			}

			Result->SetNumberField(TEXT("count"), Count);
			Result->SetNumberField(TEXT("total_scanned"), SizedAssets.Num());
			Result->SetNumberField(TEXT("total_size_bytes"), (double)TotalSize);
			Result->SetStringField(TEXT("total_size_readable"),
				TotalSize > 1048576 ? FString::Printf(TEXT("%.1f MB"), TotalSize / 1048576.0)
				: FString::Printf(TEXT("%.1f KB"), TotalSize / 1024.0));
			Result->SetArrayField(TEXT("assets"), AssetArray);

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});

	// ================================================================
	// get_asset_references - Dependency graph query
	// ================================================================
	MCP_TOOL(Registry, "get_asset_references")
		.Description(TEXT("Get the dependency graph for an asset. 'dependencies' shows what this asset depends ON (textures, materials, meshes it uses). 'referencers' shows what OTHER assets reference this one. Use to understand impact before modifying or deleting."))
		.ReadOnly()
		.Idempotent()
		.StringArg(TEXT("asset_path"), TEXT("Content path of the asset to query"), true)
		.EnumArg(TEXT("direction"), TEXT("Query direction"),
			{ TEXT("dependencies"), TEXT("referencers") }, true)
		.BoolArg(TEXT("recursive"), TEXT("Follow the chain recursively (default: false — only direct references)"))
		.IntArg(TEXT("limit"), TEXT("Maximum results (default: 50)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));

			FString Direction;
			if (!Args->TryGetStringField(TEXT("direction"), Direction))
				return FMCPToolResult::Error(TEXT("direction is required"));

			bool bRecursive = false;
			Args->TryGetBoolField(TEXT("recursive"), bRecursive);

			int32 Limit = 50;
			if (Args->HasField(TEXT("limit")))
				Limit = FMath::Clamp((int32)Args->GetNumberField(TEXT("limit")), 1, 500);

			FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
			IAssetRegistry& AR = ARM.Get();

			// Convert asset path to package name
			FString PackageName = FPackageName::ObjectPathToPackageName(AssetPath);

			TArray<FName> References;

			if (Direction == TEXT("dependencies"))
			{
				if (bRecursive)
				{
					TSet<FName> Visited;
					TArray<FName> ToVisit;
					ToVisit.Add(FName(*PackageName));

					while (ToVisit.Num() > 0 && References.Num() < Limit)
					{
						FName Current = ToVisit.Pop();
						if (Visited.Contains(Current)) continue;
						Visited.Add(Current);

						TArray<FName> Deps;
						AR.GetDependencies(Current, Deps);
						for (const FName& Dep : Deps)
						{
							if (!Dep.ToString().StartsWith(TEXT("/Game"))) continue;
							if (!Visited.Contains(Dep))
							{
								References.AddUnique(Dep);
								ToVisit.Add(Dep);
							}
						}
					}
				}
				else
				{
					AR.GetDependencies(FName(*PackageName), References);
					// Filter to game content only
					References.RemoveAll([](const FName& N) { return !N.ToString().StartsWith(TEXT("/Game")); });
				}
			}
			else // referencers
			{
				if (bRecursive)
				{
					TSet<FName> Visited;
					TArray<FName> ToVisit;
					ToVisit.Add(FName(*PackageName));

					while (ToVisit.Num() > 0 && References.Num() < Limit)
					{
						FName Current = ToVisit.Pop();
						if (Visited.Contains(Current)) continue;
						Visited.Add(Current);

						TArray<FName> Refs;
						AR.GetReferencers(Current, Refs);
						for (const FName& Ref : Refs)
						{
							if (!Ref.ToString().StartsWith(TEXT("/Game"))) continue;
							if (!Visited.Contains(Ref))
							{
								References.AddUnique(Ref);
								ToVisit.Add(Ref);
							}
						}
					}
				}
				else
				{
					AR.GetReferencers(FName(*PackageName), References);
					References.RemoveAll([](const FName& N) { return !N.ToString().StartsWith(TEXT("/Game")); });
				}
			}

			// Trim to limit
			if (References.Num() > Limit)
			{
				References.SetNum(Limit);
			}

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("asset"), AssetPath);
			Result->SetStringField(TEXT("direction"), Direction);
			Result->SetBoolField(TEXT("recursive"), bRecursive);
			Result->SetNumberField(TEXT("count"), References.Num());

			TArray<TSharedPtr<FJsonValue>> RefArray;
			for (const FName& Ref : References)
			{
				TSharedPtr<FJsonObject> RObj = MakeShared<FJsonObject>();
				RObj->SetStringField(TEXT("package"), Ref.ToString());

				// Try to get the asset class
				TArray<FAssetData> Assets;
				AR.GetAssetsByPackageName(Ref, Assets);
				if (Assets.Num() > 0)
				{
					RObj->SetStringField(TEXT("name"), Assets[0].AssetName.ToString());
					RObj->SetStringField(TEXT("class"), Assets[0].AssetClassPath.GetAssetName().ToString());
				}

				RefArray.Add(MakeShared<FJsonValueObject>(RObj));
			}
			Result->SetArrayField(TEXT("references"), RefArray);

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});

	// ================================================================
	// find_unused_assets - Find assets with zero referencers
	// ================================================================
	MCP_TOOL(Registry, "find_unused_assets")
		.Description(TEXT("Find assets that are not referenced by any other asset in the project. These are candidates for cleanup/deletion. Note: some assets may be referenced at runtime via soft references or data tables."))
		.ReadOnly()
		.Idempotent()
		.StringArg(TEXT("path"), TEXT("Content path to scan (default: '/Game/')"))
		.StringArg(TEXT("class_filter"), TEXT("Filter by asset class (e.g., 'Texture2D', 'Material')"))
		.IntArg(TEXT("limit"), TEXT("Maximum results (default: 50)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString Path = TEXT("/Game/");
			Args->TryGetStringField(TEXT("path"), Path);

			FString ClassFilter;
			Args->TryGetStringField(TEXT("class_filter"), ClassFilter);

			int32 Limit = 50;
			if (Args->HasField(TEXT("limit")))
				Limit = FMath::Clamp((int32)Args->GetNumberField(TEXT("limit")), 1, 200);

			FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
			IAssetRegistry& AR = ARM.Get();

			TArray<FAssetData> AllAssets;
			AR.GetAssetsByPath(FName(*Path), AllAssets, true);

			TArray<TSharedPtr<FJsonValue>> UnusedArray;
			int32 Scanned = 0;

			for (const FAssetData& Asset : AllAssets)
			{
				if (UnusedArray.Num() >= Limit) break;

				if (!ClassFilter.IsEmpty())
				{
					if (!Asset.AssetClassPath.GetAssetName().ToString().Contains(ClassFilter))
						continue;
				}

				Scanned++;

				// Check referencers
				TArray<FName> Referencers;
				AR.GetReferencers(Asset.PackageName, Referencers);

				// Filter to game content referencers only
				bool bHasGameReferencers = false;
				for (const FName& Ref : Referencers)
				{
					if (Ref.ToString().StartsWith(TEXT("/Game")) && Ref != Asset.PackageName)
					{
						bHasGameReferencers = true;
						break;
					}
				}

				if (!bHasGameReferencers)
				{
					TSharedPtr<FJsonObject> AObj = MakeShared<FJsonObject>();
					AObj->SetStringField(TEXT("name"), Asset.AssetName.ToString());
					AObj->SetStringField(TEXT("path"), Asset.GetObjectPathString());
					AObj->SetStringField(TEXT("class"), Asset.AssetClassPath.GetAssetName().ToString());
					UnusedArray.Add(MakeShared<FJsonValueObject>(AObj));
				}
			}

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetNumberField(TEXT("unused_count"), UnusedArray.Num());
			Result->SetNumberField(TEXT("scanned"), Scanned);
			Result->SetArrayField(TEXT("unused_assets"), UnusedArray);
			Result->SetStringField(TEXT("note"), TEXT("Some assets may be referenced via soft references, data tables, or runtime loading. Verify before deleting."));

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});

	// ================================================================
	// import_asset_with_settings - Import with detailed settings
	// ================================================================
	MCP_TOOL(Registry, "import_asset_with_settings")
		.Description(TEXT("Import an asset file (FBX, OBJ, PNG, TGA, WAV, etc.) into the project with detailed import settings. For FBX: control mesh type, material/texture import, mesh combining, and animation import. For textures: control compression and sRGB. Uses Unreal's automated import pipeline."))
		.StringArg(TEXT("source_path"), TEXT("Absolute filesystem path to the file to import (e.g., 'C:/Art/character.fbx')"), true)
		.StringArg(TEXT("destination_path"), TEXT("Content path for the imported asset (e.g., '/Game/Meshes/')"), true)
		.EnumArg(TEXT("import_as"), TEXT("For FBX files: import as StaticMesh or SkeletalMesh"),
			{ TEXT("StaticMesh"), TEXT("SkeletalMesh") })
		.BoolArg(TEXT("import_materials"), TEXT("Import materials from FBX (default: true)"))
		.BoolArg(TEXT("import_textures"), TEXT("Import textures from FBX (default: true)"))
		.BoolArg(TEXT("combine_meshes"), TEXT("Combine all meshes into one (default: false)"))
		.BoolArg(TEXT("import_animations"), TEXT("Import animations from FBX (default: true)"))
		.BoolArg(TEXT("auto_generate_collision"), TEXT("Auto-generate collision for static meshes (default: true)"))
		.EnumArg(TEXT("collision_policy"), TEXT("v5: when the destination asset already exists: replace (default, previous behaviour), skip (return the existing asset without importing) or error (refuse)"), { TEXT("replace"), TEXT("skip"), TEXT("error") })
		.EnumArg(TEXT("texture_compression"), TEXT("Texture compression after import"),
			{ TEXT("Default"), TEXT("NormalMap"), TEXT("Masks"), TEXT("HDR"), TEXT("UserInterface2D") })
		.BoolArg(TEXT("srgb"), TEXT("sRGB for imported textures"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString SourcePath, DestPath;
			if (!Args->TryGetStringField(TEXT("source_path"), SourcePath))
				return FMCPToolResult::Error(TEXT("source_path is required"));
			if (!Args->TryGetStringField(TEXT("destination_path"), DestPath))
				return FMCPToolResult::Error(TEXT("destination_path is required"));

			// v5 increment 26 (V5-27): explicit collision policy, decided before any import work.
			{
				const FString Policy = Args->HasField(TEXT("collision_policy")) ? Args->GetStringField(TEXT("collision_policy")) : TEXT("replace");
				FString Folder = DestPath; Folder.RemoveFromEnd(TEXT("/"));
				FString BaseName = FPaths::GetBaseFilename(SourcePath); for (TCHAR& C : BaseName) if (!FChar::IsAlnum(C) && C != TEXT('_')) C = TEXT('_');
				const FString ExpectedPackage = Folder / BaseName;
				const bool bExists = FPackageName::DoesPackageExist(ExpectedPackage) || FindObject<UObject>(nullptr, *(ExpectedPackage + TEXT(".") + BaseName)) != nullptr;
				if (bExists && Policy == TEXT("error"))
					return FMCPToolResult::ErrorStructured(EMCPError::AlreadyExists, FString::Printf(TEXT("Destination %s already exists and collision_policy is error"), *ExpectedPackage), TEXT("Use preview_asset_import to inspect the collision; pass collision_policy=replace or skip to proceed."));
				if (bExists && Policy == TEXT("skip"))
				{
					auto Out = MakeShared<FJsonObject>(); Out->SetBoolField(TEXT("skipped"), true); Out->SetBoolField(TEXT("imported"), false);
					Out->SetStringField(TEXT("existing_asset"), ExpectedPackage + TEXT(".") + BaseName); Out->SetStringField(TEXT("collision_policy"), Policy);
					return FMCPToolResult::SuccessStructured(FString::Printf(TEXT("Skipped: %s already exists (collision_policy=skip)."), *ExpectedPackage), Out);
				}
			}
			// Verify source file exists
			if (!FPaths::FileExists(SourcePath))
				return FMCPToolResult::Error(FString::Printf(TEXT("Source file not found: %s"), *SourcePath));

			FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
			IAssetTools& AssetTools = AssetToolsModule.Get();

			// Build automated import data
			UAutomatedAssetImportData* ImportData = NewObject<UAutomatedAssetImportData>();
			ImportData->Filenames.Add(SourcePath);
			ImportData->DestinationPath = DestPath;
			ImportData->bReplaceExisting = true;

			// Determine file type
			FString Extension = FPaths::GetExtension(SourcePath).ToLower();
			bool bIsFBX = (Extension == TEXT("fbx"));

			if (bIsFBX)
			{
				// Create FBX import factory with settings
				UFbxImportUI* FbxImportUI = NewObject<UFbxImportUI>();

				FString ImportAs;
				if (Args->TryGetStringField(TEXT("import_as"), ImportAs))
				{
					if (ImportAs == TEXT("SkeletalMesh"))
					{
						FbxImportUI->MeshTypeToImport = FBXIT_SkeletalMesh;
						FbxImportUI->bImportAsSkeletal = true;
						FbxImportUI->bImportMesh = true;
					}
					else
					{
						FbxImportUI->MeshTypeToImport = FBXIT_StaticMesh;
						FbxImportUI->bImportAsSkeletal = false;
						FbxImportUI->bImportMesh = true;
					}
				}

				bool bVal;
				if (Args->TryGetBoolField(TEXT("import_materials"), bVal))
					FbxImportUI->bImportMaterials = bVal;
				if (Args->TryGetBoolField(TEXT("import_textures"), bVal))
					FbxImportUI->bImportTextures = bVal;
				if (Args->TryGetBoolField(TEXT("import_animations"), bVal))
					FbxImportUI->bImportAnimations = bVal;

				if (FbxImportUI->StaticMeshImportData)
				{
					if (Args->TryGetBoolField(TEXT("combine_meshes"), bVal))
						FbxImportUI->StaticMeshImportData->bCombineMeshes = bVal;
					if (Args->TryGetBoolField(TEXT("auto_generate_collision"), bVal))
						FbxImportUI->StaticMeshImportData->bAutoGenerateCollision = bVal;
				}

				FbxImportUI->bIsObjImport = false;
				FbxImportUI->bAutomatedImportShouldDetectType = false;
				ImportData->bSkipReadOnly = true;
			}

			TArray<UObject*> ImportedAssets = AssetTools.ImportAssetsAutomated(ImportData);

			if (ImportedAssets.Num() == 0)
				return FMCPToolResult::Error(FString::Printf(TEXT("Import failed for '%s'. Check Output Log for details."), *SourcePath));

			// Apply texture settings if applicable
			FString TexCompression;
			bool bSRGB;
			bool bHasTexSettings = Args->TryGetStringField(TEXT("texture_compression"), TexCompression);
			bool bHasSRGB = Args->TryGetBoolField(TEXT("srgb"), bSRGB);

			TArray<FString> ImportedNames;
			for (UObject* Obj : ImportedAssets)
			{
				if (!Obj) continue;
				ImportedNames.Add(FString::Printf(TEXT("%s (%s)"), *Obj->GetName(), *Obj->GetClass()->GetName()));

				// Apply texture settings to imported textures
				if (UTexture2D* Tex = Cast<UTexture2D>(Obj))
				{
					bool bChanged = false;
					if (bHasTexSettings)
					{
						if (TexCompression == TEXT("NormalMap")) Tex->CompressionSettings = TC_Normalmap;
						else if (TexCompression == TEXT("Masks")) Tex->CompressionSettings = TC_Masks;
						else if (TexCompression == TEXT("HDR")) Tex->CompressionSettings = TC_HDR;
						else if (TexCompression == TEXT("UserInterface2D")) Tex->CompressionSettings = TC_EditorIcon;
						bChanged = true;
					}
					if (bHasSRGB)
					{
						Tex->SRGB = bSRGB;
						bChanged = true;
					}
					if (bChanged)
					{
						Tex->PostEditChange();
						Tex->MarkPackageDirty();
					}
				}
			}

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Imported %d asset(s) from '%s' to '%s': %s"),
				ImportedAssets.Num(), *FPaths::GetCleanFilename(SourcePath), *DestPath,
				*FString::Join(ImportedNames, TEXT(", "))));
		});

	// ================================================================
	// set_texture_settings - Modify texture import/compression settings
	// ================================================================
	MCP_TOOL(Registry, "set_texture_settings")
		.Description(TEXT("Modify texture settings after import: compression type, sRGB, max resolution, LOD group, and mipmap generation. Only provided fields are changed. The texture is re-saved after modification."))
		.Idempotent()
		.StringArg(TEXT("asset_path"), TEXT("Content path to the Texture2D asset"), true)
		.EnumArg(TEXT("compression"), TEXT("Texture compression setting"),
			{ TEXT("Default"), TEXT("NormalMap"), TEXT("Masks"), TEXT("HDR"), TEXT("UserInterface2D"), TEXT("Alpha"), TEXT("BC7") })
		.BoolArg(TEXT("srgb"), TEXT("Whether texture uses sRGB color space (true for color textures, false for data/masks)"))
		.IntArg(TEXT("max_size"), TEXT("Maximum texture dimension (256, 512, 1024, 2048, 4096, 8192)"))
		.EnumArg(TEXT("lod_group"), TEXT("Texture LOD group"),
			{ TEXT("World"), TEXT("WorldNormalMap"), TEXT("WorldSpecular"), TEXT("Character"), TEXT("CharacterNormalMap"), TEXT("Weapon"), TEXT("UI"), TEXT("Effects") })
		.BoolArg(TEXT("generate_mipmaps"), TEXT("Whether to generate mipmaps (default: true)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));

			UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *AssetPath);
			if (!Texture)
				return FMCPToolResult::Error(FString::Printf(TEXT("Texture not found: %s"), *AssetPath));

			Texture->PreEditChange(nullptr);

			TArray<FString> Changed;

			// Compression
			FString CompStr;
			if (Args->TryGetStringField(TEXT("compression"), CompStr))
			{
				if (CompStr == TEXT("Default")) Texture->CompressionSettings = TC_Default;
				else if (CompStr == TEXT("NormalMap")) Texture->CompressionSettings = TC_Normalmap;
				else if (CompStr == TEXT("Masks")) Texture->CompressionSettings = TC_Masks;
				else if (CompStr == TEXT("HDR")) Texture->CompressionSettings = TC_HDR;
				else if (CompStr == TEXT("UserInterface2D")) Texture->CompressionSettings = TC_EditorIcon;
				else if (CompStr == TEXT("Alpha")) Texture->CompressionSettings = TC_Alpha;
				else if (CompStr == TEXT("BC7")) Texture->CompressionSettings = TC_BC7;
				Changed.Add(FString::Printf(TEXT("Compression=%s"), *CompStr));
			}

			// sRGB
			bool bSRGB;
			if (Args->TryGetBoolField(TEXT("srgb"), bSRGB))
			{
				Texture->SRGB = bSRGB;
				Changed.Add(FString::Printf(TEXT("sRGB=%s"), bSRGB ? TEXT("true") : TEXT("false")));
			}

			// Max size
			if (Args->HasField(TEXT("max_size")))
			{
				int32 MaxSize = (int32)Args->GetNumberField(TEXT("max_size"));
				Texture->MaxTextureSize = MaxSize;
				Changed.Add(FString::Printf(TEXT("MaxSize=%d"), MaxSize));
			}

			// LOD Group
			FString LODGroup;
			if (Args->TryGetStringField(TEXT("lod_group"), LODGroup))
			{
				if (LODGroup == TEXT("World")) Texture->LODGroup = TEXTUREGROUP_World;
				else if (LODGroup == TEXT("WorldNormalMap")) Texture->LODGroup = TEXTUREGROUP_WorldNormalMap;
				else if (LODGroup == TEXT("WorldSpecular")) Texture->LODGroup = TEXTUREGROUP_WorldSpecular;
				else if (LODGroup == TEXT("Character")) Texture->LODGroup = TEXTUREGROUP_Character;
				else if (LODGroup == TEXT("CharacterNormalMap")) Texture->LODGroup = TEXTUREGROUP_CharacterNormalMap;
				else if (LODGroup == TEXT("Weapon")) Texture->LODGroup = TEXTUREGROUP_Weapon;
				else if (LODGroup == TEXT("UI")) Texture->LODGroup = TEXTUREGROUP_UI;
				else if (LODGroup == TEXT("Effects")) Texture->LODGroup = TEXTUREGROUP_Effects;
				Changed.Add(FString::Printf(TEXT("LODGroup=%s"), *LODGroup));
			}

			// Mipmaps
			bool bMipmaps;
			if (Args->TryGetBoolField(TEXT("generate_mipmaps"), bMipmaps))
			{
				Texture->MipGenSettings = bMipmaps ? TMGS_FromTextureGroup : TMGS_NoMipmaps;
				Changed.Add(FString::Printf(TEXT("Mipmaps=%s"), bMipmaps ? TEXT("true") : TEXT("false")));
			}

			if (Changed.Num() == 0)
			{
				return FMCPToolResult::Success(TEXT("No texture settings changed. Provide at least one parameter."));
			}

			Texture->PostEditChange();
			Texture->MarkPackageDirty();

			// Save
			FString PackagePath = FPackageName::ObjectPathToPackageName(Texture->GetPathName());
			FString PackageFilename = FPackageName::LongPackageNameToFilename(PackagePath, FPackageName::GetAssetPackageExtension());
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
			UPackage::SavePackage(Texture->GetPackage(), Texture, *PackageFilename, SaveArgs);

			return FMCPToolResult::Success(FString::Printf(TEXT("Updated texture '%s': %s"),
				*Texture->GetName(), *FString::Join(Changed, TEXT(", "))));
		});
}

} // namespace MCPAssetManagementTools
