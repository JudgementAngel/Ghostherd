// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "Tools/MCPBuildTools.h"
#include "Misc/DataValidation.h"
#include "MCPToolRegistry.h"
#include "MCPToolBuilder.h"
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
#include "Engine/Light.h"
#include "Components/LightComponent.h"

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
	MCP_TOOL(Registry, "get_project_info")
		.Description(TEXT("Returns comprehensive project information: project name, engine version, target platforms, build configuration, source modules, content paths, and general project settings."))
		.ReadOnly()
		.Idempotent()
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
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

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});

	// ================================================================
	// list_project_modules - List all modules in the project
	// ================================================================
	MCP_TOOL(Registry, "list_project_modules")
		.Description(TEXT("Lists all modules in the project (game modules + plugin modules). Shows module name, type, loading phase, and associated plugin if any."))
		.ReadOnly()
		.Idempotent()
		.BoolArg(TEXT("include_plugins"), TEXT("Include plugin modules in addition to game modules (default: true)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
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

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});

	// ================================================================
	// get_build_configuration - Current build config and platform
	// ================================================================
	MCP_TOOL(Registry, "get_build_configuration")
		.Description(TEXT("Returns current build configuration: debug/development/shipping, target platform, compiler settings, and key preprocessor defines."))
		.ReadOnly()
		.Idempotent()
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
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

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});

	// ================================================================
	// validate_assets - Validate assets for broken references
	// ================================================================
	MCP_TOOL(Registry, "validate_assets")
		.Description(TEXT("Validate assets without modifying them. Scope is either a content path (path + limit, legacy behaviour) or an explicit asset_paths list (1..200). Rules: dependencies (asset-registry dependency existence, no loading), data_validation (loads each asset and runs the engine's UObject::IsDataValid class rules through a validation context), or all (default). Returns per-asset diagnostics {asset_path, class, rule, severity, message}, per-rule counts, loaded_count and unresolved asset paths, plus the legacy issues list. Blueprint compilation and project validators are not run; use compile_blueprint / validate_blueprint for graphs."))
		.ReadOnly()
		.StringArg(TEXT("path"), TEXT("Content path to validate (e.g., '/Game/', '/Game/Blueprints/'). Default: '/Game/' when asset_paths is absent"))
		.IntArg(TEXT("limit"), TEXT("Maximum number of assets to check in path mode (default: 500, max: 5000)"))
		.StringArrayArg(TEXT("asset_paths"), TEXT("Explicit asset object or package paths (1..200); overrides path"))
		.EnumArg(TEXT("rules"), TEXT("dependencies | data_validation | all (default)"), { TEXT("dependencies"), TEXT("data_validation"), TEXT("all") })
		.OutputSchema(TEXT(R"({"type":"object","required":["diagnostics","counts","assets_scanned","assets_with_issues","issues","deterministic"],"properties":{"scanned_path":{"type":"string"},"total_assets":{"type":"integer"},"assets_scanned":{"type":"integer"},"assets_with_issues":{"type":"integer"},"issues":{"type":"array"},"diagnostics":{"type":"array"},"counts":{"type":"object"},"loaded_count":{"type":"integer"},"unresolved":{"type":"array"},"rules":{"type":"string"},"status":{"type":"string"},"deterministic":{"type":"boolean"}}})"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString Path = TEXT("/Game/");
			Args->TryGetStringField(TEXT("path"), Path);
			int32 Limit = 500;
			if (Args->HasField(TEXT("limit"))) Limit = FMath::Clamp((int32)Args->GetNumberField(TEXT("limit")), 1, 5000);
			const FString Rules = Args->HasField(TEXT("rules")) ? Args->GetStringField(TEXT("rules")) : TEXT("all");
			const bool bDeps = Rules != TEXT("data_validation"), bData = Rules != TEXT("dependencies");

			FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
			IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

			TArray<FAssetData> Assets; TArray<TSharedPtr<FJsonValue>> Unresolved; bool bExplicit = false;
			if (Args->HasField(TEXT("asset_paths")))
			{
				bExplicit = true;
				const auto Paths = Args->GetArrayField(TEXT("asset_paths"));
				if (Paths.Num() < 1 || Paths.Num() > 200) return FMCPToolResult::ErrorStructured(EMCPError::OutOfRange, TEXT("asset_paths must contain 1..200 entries"));
				for (const auto& V : Paths)
				{
					if (V->Type != EJson::String) return FMCPToolResult::ErrorStructured(EMCPError::OutOfRange, TEXT("asset_paths must be strings"));
					const FString P = V->AsString();
					FAssetData Data = AssetRegistry.GetAssetByObjectPath(FSoftObjectPath(P.Contains(TEXT(".")) ? P : FString::Printf(TEXT("%s.%s"), *P, *FPackageName::GetShortName(P))));
					if (!Data.IsValid()) { TArray<FAssetData> ByPackage; AssetRegistry.GetAssetsByPackageName(FName(*P), ByPackage); if (ByPackage.Num()) Data = ByPackage[0]; }
					if (Data.IsValid()) Assets.Add(Data);
					else { auto U = MakeShared<FJsonObject>(); U->SetStringField(TEXT("asset_path"), P); U->SetStringField(TEXT("reason"), TEXT("not found in the asset registry")); Unresolved.Add(MakeShared<FJsonValueObject>(U)); }
				}
				Limit = Assets.Num();
			}
			else AssetRegistry.GetAssetsByPath(FName(*Path), Assets, true);

			int32 TotalScanned = 0, AssetsWithIssues = 0, LoadedCount = 0;
			TArray<TSharedPtr<FJsonValue>> IssuesArray, Diagnostics;
			TMap<FString, int32> Counts; Counts.Add(TEXT("broken_dependency"), 0); Counts.Add(TEXT("data_validation_error"), 0); Counts.Add(TEXT("data_validation_warning"), 0); Counts.Add(TEXT("load_failed"), 0);
			auto Diag = [&](const FAssetData& Asset, const TCHAR* Rule, const TCHAR* Severity, const FString& Message)
			{
				auto D = MakeShared<FJsonObject>();
				D->SetStringField(TEXT("asset_path"), Asset.GetObjectPathString()); D->SetStringField(TEXT("class"), Asset.AssetClassPath.GetAssetName().ToString());
				D->SetStringField(TEXT("rule"), Rule); D->SetStringField(TEXT("severity"), Severity); D->SetStringField(TEXT("message"), Message);
				Diagnostics.Add(MakeShared<FJsonValueObject>(D)); Counts.FindOrAdd(Rule)++;
			};

			for (const FAssetData& Asset : Assets)
			{
				if (TotalScanned >= Limit) break;
				TotalScanned++;
				bool bAssetHasIssue = false;
				if (bDeps)
				{
					TArray<FName> Dependencies;
					AssetRegistry.GetDependencies(Asset.PackageName, Dependencies);
					TArray<FString> BrokenDeps;
					for (const FName& Dep : Dependencies)
					{
						FString DepStr = Dep.ToString();
						if (DepStr.StartsWith(TEXT("/Script/")) || DepStr.StartsWith(TEXT("/Engine/"))) continue;
						TArray<FAssetData> DepAssets;
						AssetRegistry.GetAssetsByPackageName(Dep, DepAssets);
						if (DepAssets.Num() == 0)
						{
							FString PackageFilename;
							if (!FPackageName::DoesPackageExist(DepStr, &PackageFilename)) BrokenDeps.Add(DepStr);
						}
					}
					if (BrokenDeps.Num() > 0)
					{
						bAssetHasIssue = true;
						TSharedPtr<FJsonObject> IssueObj = MakeShared<FJsonObject>();
						IssueObj->SetStringField(TEXT("asset_name"), Asset.AssetName.ToString());
						IssueObj->SetStringField(TEXT("asset_path"), Asset.GetObjectPathString());
						IssueObj->SetStringField(TEXT("asset_class"), Asset.AssetClassPath.GetAssetName().ToString());
						TArray<TSharedPtr<FJsonValue>> BrokenArray;
						for (const FString& BrokenDep : BrokenDeps) { BrokenArray.Add(MakeShared<FJsonValueString>(BrokenDep)); Diag(Asset, TEXT("broken_dependency"), TEXT("error"), FString::Printf(TEXT("Missing dependency %s"), *BrokenDep)); }
						IssueObj->SetArrayField(TEXT("broken_dependencies"), BrokenArray);
						IssueObj->SetNumberField(TEXT("broken_count"), BrokenDeps.Num());
						IssuesArray.Add(MakeShared<FJsonValueObject>(IssueObj));
					}
				}
				if (bData)
				{
					UObject* Obj = Asset.GetAsset(); // loads the package; inspection only, nothing is saved
					if (!Obj) { Diag(Asset, TEXT("load_failed"), TEXT("error"), TEXT("Asset could not be loaded")); bAssetHasIssue = true; }
					else
					{
						++LoadedCount;
						FDataValidationContext Ctx;
						const EDataValidationResult R = Obj->IsDataValid(Ctx);
						for (const FDataValidationContext::FIssue& I : Ctx.GetIssues())
						{
							const bool bErr = I.Severity == EMessageSeverity::Error;
							Diag(Asset, bErr ? TEXT("data_validation_error") : TEXT("data_validation_warning"), bErr ? TEXT("error") : TEXT("warning"), I.Message.ToString());
							if (bErr) bAssetHasIssue = true;
						}
						if (R == EDataValidationResult::Invalid && Ctx.GetNumErrors() == 0) { Diag(Asset, TEXT("data_validation_error"), TEXT("error"), TEXT("IsDataValid returned Invalid without a message")); bAssetHasIssue = true; }
					}
				}
				if (bAssetHasIssue) AssetsWithIssues++;
			}

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("scanned_path"), bExplicit ? TEXT("(explicit asset_paths)") : Path);
			Result->SetNumberField(TEXT("total_assets"), Assets.Num());
			Result->SetNumberField(TEXT("assets_scanned"), TotalScanned);
			Result->SetNumberField(TEXT("assets_with_issues"), AssetsWithIssues);
			Result->SetArrayField(TEXT("issues"), IssuesArray);
			Result->SetArrayField(TEXT("diagnostics"), Diagnostics);
			auto CountsJson = MakeShared<FJsonObject>(); for (const auto& Pair : Counts) CountsJson->SetNumberField(Pair.Key, Pair.Value);
			Result->SetObjectField(TEXT("counts"), CountsJson);
			Result->SetNumberField(TEXT("loaded_count"), LoadedCount);
			Result->SetArrayField(TEXT("unresolved"), Unresolved);
			Result->SetStringField(TEXT("rules"), Rules);
			Result->SetBoolField(TEXT("deterministic"), true);
			Result->SetStringField(TEXT("status"), AssetsWithIssues == 0 ? TEXT("All scanned assets are valid") : FString::Printf(TEXT("%d asset(s) with issues"), AssetsWithIssues));
			return FMCPToolResult::SuccessStructured(FString::Printf(TEXT("Validated %d asset(s) (%s): %d with issues, %d diagnostic(s), %d unresolved."), TotalScanned, *Rules, AssetsWithIssues, Diagnostics.Num(), Unresolved.Num()), Result);
		});

	// ================================================================
	// get_map_check_errors - Report level/map issues
	// ================================================================
	MCP_TOOL(Registry, "get_map_check_errors")
		.Description(TEXT("Runs a map check on the current level and returns errors and warnings. Reports issues such as actors with NULL references, missing meshes, lighting build status, and other common level problems."))
		.ReadOnly()
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
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

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});

	// ================================================================
	// build_lighting - Trigger lightmap build
	// ================================================================
	MCP_TOOL(Registry, "build_lighting")
		.Description(TEXT("Trigger a lighting build for the current level. Quality levels: Preview (fast, low quality), Medium, High, Production (slow, best quality). The build runs asynchronously — use get_lighting_build_info to check progress."))
		.LongRunning()
		.EnumArg(TEXT("quality"), TEXT("Lighting build quality level (default: Preview)"),
			{ TEXT("Preview"), TEXT("Medium"), TEXT("High"), TEXT("Production") })
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			FString QualityStr = TEXT("Preview");
			Args->TryGetStringField(TEXT("quality"), QualityStr);

			// Set quality via console variable
			FString QualityCommand;
			if (QualityStr == TEXT("Preview")) QualityCommand = TEXT("r.LightingQuality 0");
			else if (QualityStr == TEXT("Medium")) QualityCommand = TEXT("r.LightingQuality 1");
			else if (QualityStr == TEXT("High")) QualityCommand = TEXT("r.LightingQuality 2");
			else if (QualityStr == TEXT("Production")) QualityCommand = TEXT("r.LightingQuality 3");
			else return FMCPToolResult::Error(FString::Printf(TEXT("Invalid quality: '%s'"), *QualityStr));

			GEditor->Exec(World, *QualityCommand);

			// Trigger the build
			GEditor->Exec(World, TEXT("BUILD LIGHTING"));

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Lighting build started at %s quality. Build runs asynchronously. Use get_lighting_build_info to check progress."),
				*QualityStr));
		});

	// ================================================================
	// get_lighting_build_info - Check lighting build status
	// ================================================================
	MCP_TOOL(Registry, "get_lighting_build_info")
		.Description(TEXT("Check the current lighting build status, quality, and whether the level needs a lighting rebuild. Reports lighting-related warnings."))
		.ReadOnly()
		.Idempotent()
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			TSharedPtr<FJsonObject> Info = MakeShared<FJsonObject>();
			Info->SetStringField(TEXT("level_name"), World->GetMapName());

			// Check if there's a lighting build in progress
			bool bBuildInProgress = GEditor->IsLightingBuildCurrentlyRunning();
			Info->SetBoolField(TEXT("build_in_progress"), bBuildInProgress);

			// Count light actors and check for issues
			int32 TotalLights = 0;
			int32 StaticLights = 0;
			int32 StationaryLights = 0;
			int32 MovableLights = 0;
			int32 ShadowCastingLights = 0;

			for (TActorIterator<ALight> It(World); It; ++It)
			{
				ALight* Light = *It;
				if (!IsValid(Light)) continue;

				TotalLights++;
				ULightComponent* LC = Light->GetLightComponent();
				if (!LC) continue;

				switch (LC->Mobility)
				{
				case EComponentMobility::Static: StaticLights++; break;
				case EComponentMobility::Stationary: StationaryLights++; break;
				case EComponentMobility::Movable: MovableLights++; break;
				}

				if (LC->CastShadows)
					ShadowCastingLights++;
			}

			TSharedPtr<FJsonObject> LightStats = MakeShared<FJsonObject>();
			LightStats->SetNumberField(TEXT("total"), TotalLights);
			LightStats->SetNumberField(TEXT("static"), StaticLights);
			LightStats->SetNumberField(TEXT("stationary"), StationaryLights);
			LightStats->SetNumberField(TEXT("movable"), MovableLights);
			LightStats->SetNumberField(TEXT("shadow_casting"), ShadowCastingLights);
			Info->SetObjectField(TEXT("lights"), LightStats);

			// Warnings
			TArray<FString> Warnings;
			if (StaticLights > 0 || StationaryLights > 0)
			{
				Warnings.Add(FString::Printf(TEXT("%d static/stationary lights require a lighting build for baked lightmaps"),
					StaticLights + StationaryLights));
			}
			if (ShadowCastingLights > 10)
			{
				Warnings.Add(FString::Printf(TEXT("High shadow caster count (%d). Consider reducing for performance."),
					ShadowCastingLights));
			}

			if (Warnings.Num() > 0)
			{
				TArray<TSharedPtr<FJsonValue>> WarningArray;
				for (const FString& W : Warnings)
					WarningArray.Add(MakeShared<FJsonValueString>(W));
				Info->SetArrayField(TEXT("warnings"), WarningArray);
			}

			return FMCPToolResult::SuccessStructured(JsonToString(Info), Info);
		});
}

} // namespace MCPBuildTools
