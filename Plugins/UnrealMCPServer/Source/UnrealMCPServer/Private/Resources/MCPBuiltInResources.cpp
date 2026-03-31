#include "Resources/MCPBuiltInResources.h"
#include "MCPResourceProvider.h"
#include "MCPProtocol.h"

#include "Editor.h"
#include "Selection.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Misc/App.h"
#include "Misc/EngineVersion.h"
#include "GenericPlatform/GenericPlatformMisc.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Interfaces/IProjectManager.h"
#include "GeneralProjectSettings.h"
#include "Engine/Engine.h"
#include "Engine/Light.h"
#include "Components/LightComponent.h"
#include "LevelEditor.h"
#include "EditorViewportClient.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/ConfigCacheIni.h"
#include "HAL/FileManager.h"

namespace MCPBuiltInResources
{

void RegisterAll(FMCPResourceProvider& Provider)
{
	// ================================================================
	// unreal://project/info - Project configuration
	// ================================================================
	{
		FMCPResourceDefinition Def;
		Def.Uri = TEXT("unreal://project/info");
		Def.Name = TEXT("Project Info");
		Def.Description = TEXT("Current Unreal Engine project information including engine version, project name, and configuration.");
		Def.MimeType = TEXT("application/json");

		FMCPResourceReader Reader;
		Reader.BindLambda([](const FString& Uri) -> FMCPResourceContent
		{
			TSharedPtr<FJsonObject> Info = MakeShared<FJsonObject>();

			Info->SetStringField(TEXT("engineVersion"), FEngineVersion::Current().ToString());
			Info->SetStringField(TEXT("projectName"), FApp::GetProjectName());
			Info->SetStringField(TEXT("projectDir"), FPaths::ProjectDir());
			Info->SetStringField(TEXT("projectContentDir"), FPaths::ProjectContentDir());

			const UGeneralProjectSettings* ProjectSettings = GetDefault<UGeneralProjectSettings>();
			if (ProjectSettings)
			{
				Info->SetStringField(TEXT("companyName"), ProjectSettings->CompanyName);
				Info->SetStringField(TEXT("description"), ProjectSettings->Description);
				Info->SetStringField(TEXT("projectVersion"), ProjectSettings->ProjectVersion);
			}

			// Platform info
			Info->SetStringField(TEXT("platform"), FPlatformProperties::IniPlatformName());
			Info->SetNumberField(TEXT("cpuCores"), FPlatformMisc::NumberOfCores());

			FMCPResourceContent Content;
			Content.Uri = Uri;
			Content.MimeType = TEXT("application/json");
			Content.Text = JsonToString(Info);
			return Content;
		});

		Provider.RegisterResource(Def, Reader);
	}

	// ================================================================
	// unreal://level/current - Current level information
	// ================================================================
	{
		FMCPResourceDefinition Def;
		Def.Uri = TEXT("unreal://level/current");
		Def.Name = TEXT("Current Level");
		Def.Description = TEXT("Information about the currently loaded level including actor list.");
		Def.MimeType = TEXT("application/json");

		FMCPResourceReader Reader;
		Reader.BindLambda([](const FString& Uri) -> FMCPResourceContent
		{
			TSharedPtr<FJsonObject> Info = MakeShared<FJsonObject>();

			UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
			if (World)
			{
				Info->SetStringField(TEXT("name"), World->GetName());
				Info->SetStringField(TEXT("mapName"), World->GetMapName());

				int32 ActorCount = 0;
				TMap<FString, int32> ClassCounts;

				for (TActorIterator<AActor> It(World); It; ++It)
				{
					ActorCount++;
					FString ClassName = (*It)->GetClass()->GetName();
					ClassCounts.FindOrAdd(ClassName)++;
				}

				Info->SetNumberField(TEXT("totalActorCount"), ActorCount);

				TSharedPtr<FJsonObject> Classes = MakeShared<FJsonObject>();
				for (const auto& Pair : ClassCounts)
				{
					Classes->SetNumberField(Pair.Key, Pair.Value);
				}
				Info->SetObjectField(TEXT("actorClassDistribution"), Classes);
			}
			else
			{
				Info->SetStringField(TEXT("error"), TEXT("No editor world available"));
			}

			FMCPResourceContent Content;
			Content.Uri = Uri;
			Content.MimeType = TEXT("application/json");
			Content.Text = JsonToString(Info);
			return Content;
		});

		Provider.RegisterResource(Def, Reader);
	}

	// ================================================================
	// unreal://assets/summary - Asset registry summary
	// ================================================================
	{
		FMCPResourceDefinition Def;
		Def.Uri = TEXT("unreal://assets/summary");
		Def.Name = TEXT("Asset Summary");
		Def.Description = TEXT("Summary of all assets in the project by type and path.");
		Def.MimeType = TEXT("application/json");

		FMCPResourceReader Reader;
		Reader.BindLambda([](const FString& Uri) -> FMCPResourceContent
		{
			FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
			IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

			TArray<FAssetData> AllAssets;
			AssetRegistry.GetAssetsByPath(FName(TEXT("/Game")), AllAssets, true);

			TMap<FString, int32> ClassCounts;
			TMap<FString, int32> FolderCounts;

			for (const FAssetData& Asset : AllAssets)
			{
				ClassCounts.FindOrAdd(Asset.AssetClassPath.GetAssetName().ToString())++;

				FString Folder = FPackageName::GetLongPackagePath(Asset.PackageName.ToString());
				FolderCounts.FindOrAdd(Folder)++;
			}

			TSharedPtr<FJsonObject> Info = MakeShared<FJsonObject>();
			Info->SetNumberField(TEXT("totalAssets"), AllAssets.Num());

			TSharedPtr<FJsonObject> ByClass = MakeShared<FJsonObject>();
			for (const auto& Pair : ClassCounts)
			{
				ByClass->SetNumberField(Pair.Key, Pair.Value);
			}
			Info->SetObjectField(TEXT("byClass"), ByClass);

			// Top 20 folders
			FolderCounts.ValueSort([](int32 A, int32 B) { return A > B; });
			TSharedPtr<FJsonObject> ByFolder = MakeShared<FJsonObject>();
			int32 FolderIdx = 0;
			for (const auto& Pair : FolderCounts)
			{
				if (FolderIdx++ >= 20) break;
				ByFolder->SetNumberField(Pair.Key, Pair.Value);
			}
			Info->SetObjectField(TEXT("topFolders"), ByFolder);

			FMCPResourceContent Content;
			Content.Uri = Uri;
			Content.MimeType = TEXT("application/json");
			Content.Text = JsonToString(Info);
			return Content;
		});

		Provider.RegisterResource(Def, Reader);
	}

	// ================================================================
	// unreal://editor/log - Recent editor log output
	// ================================================================
	{
		FMCPResourceDefinition Def;
		Def.Uri = TEXT("unreal://editor/log");
		Def.Name = TEXT("Editor Log");
		Def.Description = TEXT("Recent Unreal Editor log output. Useful for debugging and understanding editor state.");
		Def.MimeType = TEXT("text/plain");

		FMCPResourceReader Reader;
		Reader.BindLambda([](const FString& Uri) -> FMCPResourceContent
		{
			// Read from the log file
			FString LogFilePath = FPaths::ProjectLogDir() / FApp::GetProjectName() + TEXT(".log");

			FMCPResourceContent Content;
			Content.Uri = Uri;
			Content.MimeType = TEXT("text/plain");

			FString LogContent;
			if (FFileHelper::LoadFileToString(LogContent, *LogFilePath))
			{
				// Return last 200 lines
				TArray<FString> Lines;
				LogContent.ParseIntoArrayLines(Lines);

				int32 StartLine = FMath::Max(0, Lines.Num() - 200);
				TArray<FString> RecentLines;
				for (int32 i = StartLine; i < Lines.Num(); i++)
				{
					RecentLines.Add(Lines[i]);
				}
				Content.Text = FString::Join(RecentLines, TEXT("\n"));
			}
			else
			{
				Content.Text = TEXT("Unable to read log file");
			}

			return Content;
		});

		Provider.RegisterResource(Def, Reader);
	}

	// ================================================================
	// unreal://editor/selection - Currently selected actors
	// ================================================================
	{
		FMCPResourceDefinition Def;
		Def.Uri = TEXT("unreal://editor/selection");
		Def.Name = TEXT("Editor Selection");
		Def.Description = TEXT("Currently selected actors in the editor with full details.");
		Def.MimeType = TEXT("application/json");

		FMCPResourceReader Reader;
		Reader.BindLambda([](const FString& Uri) -> FMCPResourceContent
		{
			TSharedPtr<FJsonObject> Info = MakeShared<FJsonObject>();

			USelection* Selection = GEditor ? GEditor->GetSelectedActors() : nullptr;
			TArray<TSharedPtr<FJsonValue>> Actors;

			if (Selection)
			{
				for (int32 i = 0; i < Selection->Num(); i++)
				{
					AActor* Actor = Cast<AActor>(Selection->GetSelectedObject(i));
					if (!IsValid(Actor)) continue;

					TSharedPtr<FJsonObject> ActorObj = MakeShared<FJsonObject>();
					ActorObj->SetStringField(TEXT("name"), Actor->GetActorLabel());
					ActorObj->SetStringField(TEXT("class"), Actor->GetClass()->GetName());

					FVector Loc = Actor->GetActorLocation();
					ActorObj->SetNumberField(TEXT("x"), Loc.X);
					ActorObj->SetNumberField(TEXT("y"), Loc.Y);
					ActorObj->SetNumberField(TEXT("z"), Loc.Z);

					ActorObj->SetStringField(TEXT("folder"), Actor->GetFolderPath().ToString());
					ActorObj->SetBoolField(TEXT("hidden"), Actor->IsHidden());

					Actors.Add(MakeShared<FJsonValueObject>(ActorObj));
				}
			}

			Info->SetNumberField(TEXT("count"), Actors.Num());
			Info->SetArrayField(TEXT("selected_actors"), Actors);

			FMCPResourceContent Content;
			Content.Uri = Uri;
			Content.MimeType = TEXT("application/json");
			Content.Text = JsonToString(Info);
			return Content;
		});

		Provider.RegisterResource(Def, Reader);
	}

	// ================================================================
	// unreal://editor/performance - Performance stats
	// ================================================================
	{
		FMCPResourceDefinition Def;
		Def.Uri = TEXT("unreal://editor/performance");
		Def.Name = TEXT("Performance Stats");
		Def.Description = TEXT("Current editor performance stats: FPS, memory, draw calls, triangle count.");
		Def.MimeType = TEXT("application/json");

		FMCPResourceReader Reader;
		Reader.BindLambda([](const FString& Uri) -> FMCPResourceContent
		{
			TSharedPtr<FJsonObject> Info = MakeShared<FJsonObject>();

			// FPS
			extern ENGINE_API float GAverageFPS;
			extern ENGINE_API float GAverageMS;
			Info->SetNumberField(TEXT("average_fps"), GAverageFPS);
			Info->SetNumberField(TEXT("average_ms"), GAverageMS);

			// Memory
			FPlatformMemoryStats MemStats = FPlatformMemory::GetStats();
			TSharedPtr<FJsonObject> Memory = MakeShared<FJsonObject>();
			Memory->SetNumberField(TEXT("used_physical_mb"), (double)MemStats.UsedPhysical / (1024.0 * 1024.0));
			Memory->SetNumberField(TEXT("available_physical_mb"), (double)MemStats.AvailablePhysical / (1024.0 * 1024.0));
			Memory->SetNumberField(TEXT("used_virtual_mb"), (double)MemStats.UsedVirtual / (1024.0 * 1024.0));
			Info->SetObjectField(TEXT("memory"), Memory);

			// World stats
			UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
			if (World)
			{
				int32 ActorCount = 0;
				for (TActorIterator<AActor> It(World); It; ++It)
				{
					ActorCount++;
				}
				Info->SetNumberField(TEXT("actor_count"), ActorCount);
			}

			FMCPResourceContent Content;
			Content.Uri = Uri;
			Content.MimeType = TEXT("application/json");
			Content.Text = JsonToString(Info);
			return Content;
		});

		Provider.RegisterResource(Def, Reader);
	}

	// ================================================================
	// unreal://project/settings - Project gameplay settings
	// ================================================================
	{
		FMCPResourceDefinition Def;
		Def.Uri = TEXT("unreal://project/settings");
		Def.Name = TEXT("Project Settings");
		Def.Description = TEXT("Key project settings: default map, game mode, rendering config, physics settings.");
		Def.MimeType = TEXT("application/json");

		FMCPResourceReader Reader;
		Reader.BindLambda([](const FString& Uri) -> FMCPResourceContent
		{
			TSharedPtr<FJsonObject> Info = MakeShared<FJsonObject>();

			// Maps
			FString DefaultMap;
			GConfig->GetString(TEXT("/Script/EngineSettings.GameMapsSettings"), TEXT("EditorStartupMap"), DefaultMap, GEngineIni);
			Info->SetStringField(TEXT("editorStartupMap"), DefaultMap);

			FString GameDefaultMap;
			GConfig->GetString(TEXT("/Script/EngineSettings.GameMapsSettings"), TEXT("GameDefaultMap"), GameDefaultMap, GEngineIni);
			Info->SetStringField(TEXT("gameDefaultMap"), GameDefaultMap);

			FString GlobalDefaultGameMode;
			GConfig->GetString(TEXT("/Script/EngineSettings.GameMapsSettings"), TEXT("GlobalDefaultGameMode"), GlobalDefaultGameMode, GEngineIni);
			Info->SetStringField(TEXT("globalDefaultGameMode"), GlobalDefaultGameMode);

			// Rendering
			TSharedPtr<FJsonObject> Rendering = MakeShared<FJsonObject>();
			FString DefaultRHI;
			GConfig->GetString(TEXT("/Script/WindowsTargetPlatform.WindowsTargetSettings"), TEXT("DefaultGraphicsRHI"), DefaultRHI, GEngineIni);
			Rendering->SetStringField(TEXT("rhi"), DefaultRHI);
			Info->SetObjectField(TEXT("rendering"), Rendering);

			FMCPResourceContent Content;
			Content.Uri = Uri;
			Content.MimeType = TEXT("application/json");
			Content.Text = JsonToString(Info);
			return Content;
		});

		Provider.RegisterResource(Def, Reader);
	}

	// ================================================================
	// unreal://project/plugins - Enabled plugins
	// ================================================================
	{
		FMCPResourceDefinition Def;
		Def.Uri = TEXT("unreal://project/plugins");
		Def.Name = TEXT("Enabled Plugins");
		Def.Description = TEXT("List of enabled plugins in the current project with version info.");
		Def.MimeType = TEXT("application/json");

		FMCPResourceReader Reader;
		Reader.BindLambda([](const FString& Uri) -> FMCPResourceContent
		{
			TSharedPtr<FJsonObject> Info = MakeShared<FJsonObject>();
			TArray<TSharedPtr<FJsonValue>> PluginArray;

			TArray<TSharedRef<IPlugin>> Plugins = IPluginManager::Get().GetEnabledPlugins();
			for (const TSharedRef<IPlugin>& Plugin : Plugins)
			{
				TSharedPtr<FJsonObject> PluginObj = MakeShared<FJsonObject>();
				PluginObj->SetStringField(TEXT("name"), Plugin->GetName());
				PluginObj->SetBoolField(TEXT("enabled"), Plugin->IsEnabled());
				PluginArray.Add(MakeShared<FJsonValueObject>(PluginObj));
			}

			Info->SetNumberField(TEXT("count"), PluginArray.Num());
			Info->SetArrayField(TEXT("plugins"), PluginArray);

			FMCPResourceContent Content;
			Content.Uri = Uri;
			Content.MimeType = TEXT("application/json");
			Content.Text = JsonToString(Info);
			return Content;
		});

		Provider.RegisterResource(Def, Reader);
	}

	// ================================================================
	// unreal://editor/viewport - Viewport camera state
	// ================================================================
	{
		FMCPResourceDefinition Def;
		Def.Uri = TEXT("unreal://editor/viewport");
		Def.Name = TEXT("Viewport Camera");
		Def.Description = TEXT("Current editor viewport camera position, rotation, FOV, and projection mode.");
		Def.MimeType = TEXT("application/json");

		FMCPResourceReader Reader;
		Reader.BindLambda([](const FString& Uri) -> FMCPResourceContent
		{
			TSharedPtr<FJsonObject> Info = MakeShared<FJsonObject>();

			if (GEditor && GEditor->GetActiveViewport())
			{
				FEditorViewportClient* ViewportClient = static_cast<FEditorViewportClient*>(GEditor->GetActiveViewport()->GetClient());
				if (ViewportClient)
				{
					FVector Location = ViewportClient->GetViewLocation();
					FRotator Rotation = ViewportClient->GetViewRotation();

					Info->SetNumberField(TEXT("x"), Location.X);
					Info->SetNumberField(TEXT("y"), Location.Y);
					Info->SetNumberField(TEXT("z"), Location.Z);
					Info->SetNumberField(TEXT("pitch"), Rotation.Pitch);
					Info->SetNumberField(TEXT("yaw"), Rotation.Yaw);
					Info->SetNumberField(TEXT("roll"), Rotation.Roll);
					Info->SetNumberField(TEXT("fov"), ViewportClient->ViewFOV);
					Info->SetBoolField(TEXT("isPerspective"), ViewportClient->IsPerspective());
					Info->SetStringField(TEXT("viewMode"), ViewportClient->IsRealtime() ? TEXT("Realtime") : TEXT("Standard"));
				}
			}
			else
			{
				Info->SetStringField(TEXT("error"), TEXT("No active viewport"));
			}

			FMCPResourceContent Content;
			Content.Uri = Uri;
			Content.MimeType = TEXT("application/json");
			Content.Text = JsonToString(Info);
			return Content;
		});

		Provider.RegisterResource(Def, Reader);
	}

	// ================================================================
	// unreal://level/lighting - Light actor summary
	// ================================================================
	{
		FMCPResourceDefinition Def;
		Def.Uri = TEXT("unreal://level/lighting");
		Def.Name = TEXT("Level Lighting");
		Def.Description = TEXT("Summary of all light actors in the current level with type, intensity, and color.");
		Def.MimeType = TEXT("application/json");

		FMCPResourceReader Reader;
		Reader.BindLambda([](const FString& Uri) -> FMCPResourceContent
		{
			TSharedPtr<FJsonObject> Info = MakeShared<FJsonObject>();
			TArray<TSharedPtr<FJsonValue>> LightArray;

			UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
			if (World)
			{
				for (TActorIterator<ALight> It(World); It; ++It)
				{
					ALight* Light = *It;
					if (!IsValid(Light)) continue;

					TSharedPtr<FJsonObject> LightObj = MakeShared<FJsonObject>();
					LightObj->SetStringField(TEXT("name"), Light->GetActorLabel());
					LightObj->SetStringField(TEXT("class"), Light->GetClass()->GetName());

					if (ULightComponent* LightComp = Light->GetLightComponent())
					{
						LightObj->SetNumberField(TEXT("intensity"), LightComp->Intensity);
						FLinearColor Color = LightComp->GetLightColor();
						LightObj->SetStringField(TEXT("color"), FString::Printf(TEXT("R=%.2f G=%.2f B=%.2f"), Color.R, Color.G, Color.B));
						LightObj->SetBoolField(TEXT("castShadows"), LightComp->CastShadows);
					}

					FVector Loc = Light->GetActorLocation();
					LightObj->SetNumberField(TEXT("x"), Loc.X);
					LightObj->SetNumberField(TEXT("y"), Loc.Y);
					LightObj->SetNumberField(TEXT("z"), Loc.Z);

					LightArray.Add(MakeShared<FJsonValueObject>(LightObj));
				}
			}

			Info->SetNumberField(TEXT("lightCount"), LightArray.Num());
			Info->SetArrayField(TEXT("lights"), LightArray);

			FMCPResourceContent Content;
			Content.Uri = Uri;
			Content.MimeType = TEXT("application/json");
			Content.Text = JsonToString(Info);
			return Content;
		});

		Provider.RegisterResource(Def, Reader);
	}

	// ================================================================
	// unreal://assets/recent - Recently modified assets
	// ================================================================
	{
		FMCPResourceDefinition Def;
		Def.Uri = TEXT("unreal://assets/recent");
		Def.Name = TEXT("Recent Assets");
		Def.Description = TEXT("Most recently modified assets in the project (up to 50).");
		Def.MimeType = TEXT("application/json");

		FMCPResourceReader Reader;
		Reader.BindLambda([](const FString& Uri) -> FMCPResourceContent
		{
			FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
			IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

			TArray<FAssetData> AllAssets;
			AssetRegistry.GetAssetsByPath(FName(TEXT("/Game")), AllAssets, true);

			// Sort by package file timestamp (most recent first)
			AllAssets.Sort([](const FAssetData& A, const FAssetData& B)
			{
				FString PathA = FPackageName::LongPackageNameToFilename(A.PackageName.ToString(), FPackageName::GetAssetPackageExtension());
				FString PathB = FPackageName::LongPackageNameToFilename(B.PackageName.ToString(), FPackageName::GetAssetPackageExtension());
				FDateTime TimeA = IFileManager::Get().GetTimeStamp(*PathA);
				FDateTime TimeB = IFileManager::Get().GetTimeStamp(*PathB);
				return TimeA > TimeB;
			});

			TSharedPtr<FJsonObject> Info = MakeShared<FJsonObject>();
			TArray<TSharedPtr<FJsonValue>> AssetArray;

			int32 Count = FMath::Min(AllAssets.Num(), 50);
			for (int32 i = 0; i < Count; i++)
			{
				const FAssetData& Asset = AllAssets[i];
				TSharedPtr<FJsonObject> AssetObj = MakeShared<FJsonObject>();
				AssetObj->SetStringField(TEXT("name"), Asset.AssetName.ToString());
				AssetObj->SetStringField(TEXT("path"), Asset.GetObjectPathString());
				AssetObj->SetStringField(TEXT("class"), Asset.AssetClassPath.GetAssetName().ToString());
				AssetArray.Add(MakeShared<FJsonValueObject>(AssetObj));
			}

			Info->SetNumberField(TEXT("count"), AssetArray.Num());
			Info->SetArrayField(TEXT("assets"), AssetArray);

			FMCPResourceContent Content;
			Content.Uri = Uri;
			Content.MimeType = TEXT("application/json");
			Content.Text = JsonToString(Info);
			return Content;
		});

		Provider.RegisterResource(Def, Reader);
	}

	// ================================================================
	// unreal://level/bounds - World bounding box
	// ================================================================
	{
		FMCPResourceDefinition Def;
		Def.Uri = TEXT("unreal://level/bounds");
		Def.Name = TEXT("Level Bounds");
		Def.Description = TEXT("World bounding box encompassing all actors in the current level.");
		Def.MimeType = TEXT("application/json");

		FMCPResourceReader Reader;
		Reader.BindLambda([](const FString& Uri) -> FMCPResourceContent
		{
			TSharedPtr<FJsonObject> Info = MakeShared<FJsonObject>();

			UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
			if (World)
			{
				FBox WorldBounds(ForceInit);
				int32 ActorCount = 0;

				for (TActorIterator<AActor> It(World); It; ++It)
				{
					AActor* Actor = *It;
					if (!IsValid(Actor)) continue;

					FVector Origin, Extent;
					Actor->GetActorBounds(false, Origin, Extent);

					if (Extent.SizeSquared() > 0)
					{
						FBox ActorBox(Origin - Extent, Origin + Extent);
						if (WorldBounds.IsValid)
						{
							WorldBounds += ActorBox;
						}
						else
						{
							WorldBounds = ActorBox;
						}
						ActorCount++;
					}
				}

				Info->SetNumberField(TEXT("actors_with_bounds"), ActorCount);

				if (WorldBounds.IsValid)
				{
					TSharedPtr<FJsonObject> Min = MakeShared<FJsonObject>();
					Min->SetNumberField(TEXT("x"), WorldBounds.Min.X);
					Min->SetNumberField(TEXT("y"), WorldBounds.Min.Y);
					Min->SetNumberField(TEXT("z"), WorldBounds.Min.Z);
					Info->SetObjectField(TEXT("min"), Min);

					TSharedPtr<FJsonObject> Max = MakeShared<FJsonObject>();
					Max->SetNumberField(TEXT("x"), WorldBounds.Max.X);
					Max->SetNumberField(TEXT("y"), WorldBounds.Max.Y);
					Max->SetNumberField(TEXT("z"), WorldBounds.Max.Z);
					Info->SetObjectField(TEXT("max"), Max);

					FVector Center = WorldBounds.GetCenter();
					FVector Size = WorldBounds.GetSize();
					Info->SetNumberField(TEXT("center_x"), Center.X);
					Info->SetNumberField(TEXT("center_y"), Center.Y);
					Info->SetNumberField(TEXT("center_z"), Center.Z);
					Info->SetNumberField(TEXT("size_x"), Size.X);
					Info->SetNumberField(TEXT("size_y"), Size.Y);
					Info->SetNumberField(TEXT("size_z"), Size.Z);
				}
			}
			else
			{
				Info->SetStringField(TEXT("error"), TEXT("No editor world available"));
			}

			FMCPResourceContent Content;
			Content.Uri = Uri;
			Content.MimeType = TEXT("application/json");
			Content.Text = JsonToString(Info);
			return Content;
		});

		Provider.RegisterResource(Def, Reader);
	}
}

} // namespace MCPBuiltInResources
