#include "Tools/MCPBuildTools.h"
#include "MCPToolRegistry.h"
#include "MCPProtocol.h"

#include "Editor.h"
#include "Engine/World.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "Misc/App.h"
#include "Misc/EngineVersion.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "ProjectDescriptor.h"
#include "Interfaces/IPluginManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "GeneralProjectSettings.h"
#include "GenericPlatform/GenericPlatformMisc.h"
#include "Misc/ConfigCacheIni.h"
#include "GameFramework/Actor.h"

namespace MCPBuildTools
{

static UWorld* GetEditorWorld()
{
	if (GEditor)
	{
		return GEditor->GetEditorWorldContext().World();
	}
	return nullptr;
}

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// get_project_info - Comprehensive project information
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();

		FMCPToolDefinition Def;
		Def.Name = TEXT("get_project_info");
		Def.Description = TEXT("Returns comprehensive project information: project name, engine version, target platforms, build configuration, source modules, content paths, and general project settings.");
		Def.InputSchema = Schema;
		Def.bReadOnlyHint = true;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

			// Core project info
			Result->SetStringField(TEXT("project_name"), FApp::GetProjectName());
			Result->SetStringField(TEXT("engine_version"), FEngineVersion::Current().ToString());
			Result->SetStringField(TEXT("project_dir"), FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()));
			Result->SetStringField(TEXT("content_dir"), FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir()));
			Result->SetStringField(TEXT("config_dir"), FPaths::ConvertRelativePathToFull(FPaths::ProjectConfigDir()));
			Result->SetStringField(TEXT("saved_dir"), FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()));
			Result->SetStringField(TEXT("plugins_dir"), FPaths::ConvertRelativePathToFull(FPaths::ProjectPluginsDir()));
			Result->SetStringField(TEXT("log_dir"), FPaths::ConvertRelativePathToFull(FPaths::ProjectLogDir()));

			// General project settings (from UGeneralProjectSettings)
			const UGeneralProjectSettings* ProjectSettings = GetDefault<UGeneralProjectSettings>();
			if (ProjectSettings)
			{
				Result->SetStringField(TEXT("company_name"), ProjectSettings->CompanyName);
				Result->SetStringField(TEXT("description"), ProjectSettings->Description);
				Result->SetStringField(TEXT("project_version"), ProjectSettings->ProjectVersion);
				Result->SetStringField(TEXT("homepage"), ProjectSettings->Homepage);
				Result->SetStringField(TEXT("support_contact"), ProjectSettings->SupportContact);
				Result->SetStringField(TEXT("project_id"), ProjectSettings->ProjectID.ToString());
			}

			// Load and parse the .uproject file for module info
			FString UProjectPath = FPaths::GetProjectFilePath();
			Result->SetStringField(TEXT("uproject_path"), FPaths::ConvertRelativePathToFull(UProjectPath));

			FProjectDescriptor ProjectDesc;
			FText FailReason;
			if (ProjectDesc.Load(UProjectPath, FailReason))
			{
				if (!ProjectDesc.Description.IsEmpty())
				{
					Result->SetStringField(TEXT("uproject_description"), ProjectDesc.Description);
				}
				if (!ProjectDesc.Category.IsEmpty())
				{
					Result->SetStringField(TEXT("category"), ProjectDesc.Category);
				}

				// Modules
				TArray<TSharedPtr<FJsonValue>> ModulesArray;
				for (const FModuleDescriptor& Module : ProjectDesc.Modules)
				{
					TSharedPtr<FJsonObject> ModObj = MakeShared<FJsonObject>();
					ModObj->SetStringField(TEXT("name"), Module.Name.ToString());
					ModObj->SetStringField(TEXT("type"), EHostType::ToString(Module.Type));
					ModObj->SetStringField(TEXT("loading_phase"), ELoadingPhase::ToString(Module.LoadingPhase));
					ModulesArray.Add(MakeShared<FJsonValueObject>(ModObj));
				}
				Result->SetArrayField(TEXT("modules"), ModulesArray);

				// Plugins referenced in .uproject
				TArray<TSharedPtr<FJsonValue>> PluginsArray;
				for (const FPluginReferenceDescriptor& PluginRef : ProjectDesc.Plugins)
				{
					TSharedPtr<FJsonObject> PlugObj = MakeShared<FJsonObject>();
					PlugObj->SetStringField(TEXT("name"), PluginRef.Name);
					PlugObj->SetBoolField(TEXT("enabled"), PluginRef.bEnabled);
					PluginsArray.Add(MakeShared<FJsonValueObject>(PlugObj));
				}
				Result->SetArrayField(TEXT("project_plugins"), PluginsArray);
			}

			// Platform info
			Result->SetStringField(TEXT("platform"), FPlatformProperties::IniPlatformName());
			Result->SetNumberField(TEXT("cpu_cores"), FPlatformMisc::NumberOfCores());

			return FMCPToolResult::Success(JsonToString(Result));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// list_project_modules - List all modules in the project
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddBoolean(Schema, TEXT("include_plugins"), TEXT("Include plugin modules in addition to game modules (default: true)"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("list_project_modules");
		Def.Description = TEXT("Lists all modules in the project (game modules + plugin modules). Shows module name, type, loading phase, and associated plugin if any.");
		Def.InputSchema = Schema;
		Def.bReadOnlyHint = true;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			bool bIncludePlugins = true;
			Args->TryGetBoolField(TEXT("include_plugins"), bIncludePlugins);

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

			// Game modules from .uproject
			FString UProjectPath = FPaths::GetProjectFilePath();
			FProjectDescriptor ProjectDesc;
			FText FailReason;

			TArray<TSharedPtr<FJsonValue>> GameModules;
			if (ProjectDesc.Load(UProjectPath, FailReason))
			{
				for (const FModuleDescriptor& Module : ProjectDesc.Modules)
				{
					TSharedPtr<FJsonObject> ModObj = MakeShared<FJsonObject>();
					ModObj->SetStringField(TEXT("name"), Module.Name.ToString());
					ModObj->SetStringField(TEXT("type"), EHostType::ToString(Module.Type));
					ModObj->SetStringField(TEXT("loading_phase"), ELoadingPhase::ToString(Module.LoadingPhase));
					ModObj->SetStringField(TEXT("source"), TEXT("Project"));
					GameModules.Add(MakeShared<FJsonValueObject>(ModObj));
				}
			}
			Result->SetArrayField(TEXT("game_modules"), GameModules);

			// Plugin modules
			if (bIncludePlugins)
			{
				TArray<TSharedPtr<FJsonValue>> PluginModules;
				TArray<TSharedRef<IPlugin>> EnabledPlugins = IPluginManager::Get().GetEnabledPlugins();

				for (const TSharedRef<IPlugin>& Plugin : EnabledPlugins)
				{
					const FPluginDescriptor& PluginDesc = Plugin->GetDescriptor();
					for (const FModuleDescriptor& Module : PluginDesc.Modules)
					{
						TSharedPtr<FJsonObject> ModObj = MakeShared<FJsonObject>();
						ModObj->SetStringField(TEXT("name"), Module.Name.ToString());
						ModObj->SetStringField(TEXT("type"), EHostType::ToString(Module.Type));
						ModObj->SetStringField(TEXT("loading_phase"), ELoadingPhase::ToString(Module.LoadingPhase));
						ModObj->SetStringField(TEXT("plugin"), Plugin->GetName());
						ModObj->SetStringField(TEXT("source"), TEXT("Plugin"));
						PluginModules.Add(MakeShared<FJsonValueObject>(ModObj));
					}
				}
				Result->SetArrayField(TEXT("plugin_modules"), PluginModules);
				Result->SetNumberField(TEXT("plugin_module_count"), PluginModules.Num());
			}

			Result->SetNumberField(TEXT("game_module_count"), GameModules.Num());

			return FMCPToolResult::Success(JsonToString(Result));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// get_build_configuration - Current build config and platform
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();

		FMCPToolDefinition Def;
		Def.Name = TEXT("get_build_configuration");
		Def.Description = TEXT("Returns current build configuration: debug/development/shipping, target platform, compiler settings, and key preprocessor defines.");
		Def.InputSchema = Schema;
		Def.bReadOnlyHint = true;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

			// Build configuration
			Result->SetStringField(TEXT("build_config"), LexToString(FApp::GetBuildConfiguration()));
			Result->SetStringField(TEXT("build_target"), LexToString(FApp::GetBuildTargetType()));
			Result->SetStringField(TEXT("platform"), FPlatformProperties::PlatformName());
			Result->SetStringField(TEXT("ini_platform"), FPlatformProperties::IniPlatformName());

			// Preprocessor flags
			Result->SetBoolField(TEXT("is_debug"), UE_BUILD_DEBUG != 0);
			Result->SetBoolField(TEXT("is_development"), UE_BUILD_DEVELOPMENT != 0);
			Result->SetBoolField(TEXT("is_shipping"), UE_BUILD_SHIPPING != 0);
			Result->SetBoolField(TEXT("is_test"), UE_BUILD_TEST != 0);
			Result->SetBoolField(TEXT("with_editor"), WITH_EDITOR != 0);

			// Platform properties
			TSharedPtr<FJsonObject> PlatformInfo = MakeShared<FJsonObject>();
			PlatformInfo->SetBoolField(TEXT("requires_cooked_data"), FPlatformProperties::RequiresCookedData());
			PlatformInfo->SetBoolField(TEXT("supports_windowed_mode"), FPlatformProperties::SupportsWindowedMode());
			PlatformInfo->SetBoolField(TEXT("has_editor_only_data"), FPlatformProperties::HasEditorOnlyData());
			PlatformInfo->SetBoolField(TEXT("is_server_only"), FPlatformProperties::IsServerOnly());
			PlatformInfo->SetBoolField(TEXT("is_client_only"), FPlatformProperties::IsClientOnly());
			Result->SetObjectField(TEXT("platform_properties"), PlatformInfo);

			// Hardware info
			TSharedPtr<FJsonObject> Hardware = MakeShared<FJsonObject>();
			Hardware->SetNumberField(TEXT("cpu_cores"), FPlatformMisc::NumberOfCores());
			Hardware->SetNumberField(TEXT("cpu_cores_with_hyperthreads"), FPlatformMisc::NumberOfCoresIncludingHyperthreads());

			FPlatformMemoryStats MemStats = FPlatformMemory::GetStats();
			Hardware->SetNumberField(TEXT("physical_memory_mb"), (double)MemStats.TotalPhysical / (1024.0 * 1024.0));
			Hardware->SetNumberField(TEXT("available_physical_mb"), (double)MemStats.AvailablePhysical / (1024.0 * 1024.0));
			Result->SetObjectField(TEXT("hardware"), Hardware);

			// Rendering config from ini
			FString DefaultRHI;
			GConfig->GetString(TEXT("/Script/WindowsTargetPlatform.WindowsTargetSettings"), TEXT("DefaultGraphicsRHI"), DefaultRHI, GEngineIni);
			if (!DefaultRHI.IsEmpty())
			{
				Result->SetStringField(TEXT("default_rhi"), DefaultRHI);
			}

			return FMCPToolResult::Success(JsonToString(Result));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// validate_assets - Validate assets for broken references
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("path"), TEXT("Content path to validate (e.g., '/Game/', '/Game/Blueprints/'). Default: '/Game/'"));
		FMCPSchemaBuilder::AddInteger(Schema, TEXT("limit"), TEXT("Maximum number of assets to check (default: 500, max: 5000)"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("validate_assets");
		Def.Description = TEXT("Validates assets in a given content path, checking for broken references and missing dependencies. Returns a list of assets with issues and their broken dependency details.");
		Def.InputSchema = Schema;
		Def.bReadOnlyHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString Path = TEXT("/Game/");
			Args->TryGetStringField(TEXT("path"), Path);

			int32 Limit = 500;
			if (Args->HasField(TEXT("limit")))
			{
				Limit = FMath::Clamp((int32)Args->GetNumberField(TEXT("limit")), 1, 5000);
			}

			FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
			IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

			TArray<FAssetData> Assets;
			AssetRegistry.GetAssetsByPath(FName(*Path), Assets, true);

			int32 TotalScanned = 0;
			int32 AssetsWithIssues = 0;
			TArray<TSharedPtr<FJsonValue>> IssuesArray;

			for (const FAssetData& Asset : Assets)
			{
				if (TotalScanned >= Limit) break;
				TotalScanned++;

				TArray<FName> Dependencies;
				AssetRegistry.GetDependencies(Asset.PackageName, Dependencies);

				TArray<FString> BrokenDeps;
				for (const FName& Dep : Dependencies)
				{
					FString DepStr = Dep.ToString();

					// Skip engine and script dependencies
					if (DepStr.StartsWith(TEXT("/Script/")) || DepStr.StartsWith(TEXT("/Engine/")))
						continue;

					// Check if the dependency package exists in the asset registry
					TArray<FAssetData> DepAssets;
					AssetRegistry.GetAssetsByPackageName(Dep, DepAssets);
					if (DepAssets.Num() == 0)
					{
						// Also check if it's a valid package path on disk
						FString PackageFilename;
						if (!FPackageName::DoesPackageExist(DepStr, &PackageFilename))
						{
							BrokenDeps.Add(DepStr);
						}
					}
				}

				if (BrokenDeps.Num() > 0)
				{
					AssetsWithIssues++;

					TSharedPtr<FJsonObject> IssueObj = MakeShared<FJsonObject>();
					IssueObj->SetStringField(TEXT("asset_name"), Asset.AssetName.ToString());
					IssueObj->SetStringField(TEXT("asset_path"), Asset.GetObjectPathString());
					IssueObj->SetStringField(TEXT("asset_class"), Asset.AssetClassPath.GetAssetName().ToString());

					TArray<TSharedPtr<FJsonValue>> BrokenArray;
					for (const FString& BrokenDep : BrokenDeps)
					{
						BrokenArray.Add(MakeShared<FJsonValueString>(BrokenDep));
					}
					IssueObj->SetArrayField(TEXT("broken_dependencies"), BrokenArray);
					IssueObj->SetNumberField(TEXT("broken_count"), BrokenDeps.Num());

					IssuesArray.Add(MakeShared<FJsonValueObject>(IssueObj));
				}
			}

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("scanned_path"), Path);
			Result->SetNumberField(TEXT("total_assets"), Assets.Num());
			Result->SetNumberField(TEXT("assets_scanned"), TotalScanned);
			Result->SetNumberField(TEXT("assets_with_issues"), AssetsWithIssues);
			Result->SetArrayField(TEXT("issues"), IssuesArray);

			if (AssetsWithIssues == 0)
			{
				Result->SetStringField(TEXT("status"), TEXT("All scanned assets are valid"));
			}
			else
			{
				Result->SetStringField(TEXT("status"), FString::Printf(TEXT("Found %d assets with broken dependencies"), AssetsWithIssues));
			}

			return FMCPToolResult::Success(JsonToString(Result));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// get_map_check_errors - Report level/map issues
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();

		FMCPToolDefinition Def;
		Def.Name = TEXT("get_map_check_errors");
		Def.Description = TEXT("Runs a map check on the current level and returns errors and warnings. Reports issues such as actors with NULL references, missing meshes, lighting build status, and other common level problems.");
		Def.InputSchema = Schema;
		Def.bReadOnlyHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("level_name"), World->GetMapName());

			TArray<TSharedPtr<FJsonValue>> ErrorsArray;
			TArray<TSharedPtr<FJsonValue>> WarningsArray;

			// Run map check via the editor exec command
			// This populates the message log with map check results
			GEditor->Exec(World, TEXT("MAP CHECK"));

			// Check for common actor issues
			int32 TotalActors = 0;
			int32 NullMeshActors = 0;
			int32 InvalidActors = 0;
			int32 HiddenActors = 0;

			for (TActorIterator<AActor> It(World); It; ++It)
			{
				AActor* Actor = *It;
				TotalActors++;

				if (!IsValid(Actor))
				{
					InvalidActors++;

					TSharedPtr<FJsonObject> ErrObj = MakeShared<FJsonObject>();
					ErrObj->SetStringField(TEXT("type"), TEXT("InvalidActor"));
					ErrObj->SetStringField(TEXT("message"), TEXT("Actor is pending kill or invalid"));
					ErrorsArray.Add(MakeShared<FJsonValueObject>(ErrObj));
					continue;
				}

				if (Actor->IsHidden())
				{
					HiddenActors++;
				}

				// Check for StaticMeshActors with NULL meshes
				if (AStaticMeshActor* SMActor = Cast<AStaticMeshActor>(Actor))
				{
					UStaticMeshComponent* SMComp = SMActor->GetStaticMeshComponent();
					if (SMComp && !SMComp->GetStaticMesh())
					{
						NullMeshActors++;

						TSharedPtr<FJsonObject> WarnObj = MakeShared<FJsonObject>();
						WarnObj->SetStringField(TEXT("type"), TEXT("NullStaticMesh"));
						WarnObj->SetStringField(TEXT("actor"), Actor->GetActorLabel());
						WarnObj->SetStringField(TEXT("message"), TEXT("StaticMeshActor has no mesh assigned"));
						WarningsArray.Add(MakeShared<FJsonValueObject>(WarnObj));
					}
				}

				// Check for actors with NULL root components
				if (!Actor->GetRootComponent())
				{
					TSharedPtr<FJsonObject> WarnObj = MakeShared<FJsonObject>();
					WarnObj->SetStringField(TEXT("type"), TEXT("NoRootComponent"));
					WarnObj->SetStringField(TEXT("actor"), Actor->GetActorLabel());
					WarnObj->SetStringField(TEXT("class"), Actor->GetClass()->GetName());
					WarnObj->SetStringField(TEXT("message"), TEXT("Actor has no root component"));
					WarningsArray.Add(MakeShared<FJsonValueObject>(WarnObj));
				}

				// Check for actors at extreme locations (possible placement errors)
				FVector Loc = Actor->GetActorLocation();
				const double ExtremeDist = 1000000.0; // 10 km
				if (FMath::Abs(Loc.X) > ExtremeDist || FMath::Abs(Loc.Y) > ExtremeDist || FMath::Abs(Loc.Z) > ExtremeDist)
				{
					TSharedPtr<FJsonObject> WarnObj = MakeShared<FJsonObject>();
					WarnObj->SetStringField(TEXT("type"), TEXT("ExtremeLocation"));
					WarnObj->SetStringField(TEXT("actor"), Actor->GetActorLabel());
					WarnObj->SetStringField(TEXT("message"), FString::Printf(
						TEXT("Actor at extreme location (%.0f, %.0f, %.0f) - possible placement error"),
						Loc.X, Loc.Y, Loc.Z));
					WarningsArray.Add(MakeShared<FJsonValueObject>(WarnObj));
				}
			}

			// Summary statistics
			TSharedPtr<FJsonObject> Summary = MakeShared<FJsonObject>();
			Summary->SetNumberField(TEXT("total_actors"), TotalActors);
			Summary->SetNumberField(TEXT("invalid_actors"), InvalidActors);
			Summary->SetNumberField(TEXT("null_mesh_actors"), NullMeshActors);
			Summary->SetNumberField(TEXT("hidden_actors"), HiddenActors);
			Result->SetObjectField(TEXT("summary"), Summary);

			Result->SetNumberField(TEXT("error_count"), ErrorsArray.Num());
			Result->SetArrayField(TEXT("errors"), ErrorsArray);
			Result->SetNumberField(TEXT("warning_count"), WarningsArray.Num());
			Result->SetArrayField(TEXT("warnings"), WarningsArray);

			if (ErrorsArray.Num() == 0 && WarningsArray.Num() == 0)
			{
				Result->SetStringField(TEXT("status"), TEXT("No issues found"));
			}
			else
			{
				Result->SetStringField(TEXT("status"), FString::Printf(
					TEXT("Found %d errors and %d warnings"),
					ErrorsArray.Num(), WarningsArray.Num()));
			}

			return FMCPToolResult::Success(JsonToString(Result));
		});
		Registry.RegisterTool(Def);
	}
}

} // namespace MCPBuildTools
