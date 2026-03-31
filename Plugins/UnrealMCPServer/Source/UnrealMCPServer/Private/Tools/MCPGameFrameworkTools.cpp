#include "Tools/MCPGameFrameworkTools.h"
#include "MCPToolRegistry.h"
#include "MCPProtocol.h"

#include "Engine/World.h"
#include "Editor.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/HUD.h"
#include "GameFramework/WorldSettings.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "AssetToolsModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Factories/BlueprintFactory.h"
#include "UObject/SavePackage.h"

namespace MCPGameFrameworkTools
{

// ============================================================================
// Helper: Create a Blueprint with a given parent class, save it, return result
// ============================================================================

static FMCPToolResult CreateFrameworkBlueprint(const FString& AssetPath, UClass* ParentClass, const FString& TypeLabel)
{
	if (AssetPath.IsEmpty())
	{
		return FMCPToolResult::Error(TEXT("asset_path is required"));
	}

	if (!ParentClass)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Parent class is null for %s"), *TypeLabel));
	}

	FString PackagePath = FPackageName::ObjectPathToPackageName(AssetPath);
	FString AssetName = FPackageName::GetShortName(AssetPath);

	UPackage* Package = CreatePackage(*PackagePath);
	if (!Package)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Failed to create package: %s"), *PackagePath));
	}

	UBlueprintFactory* Factory = NewObject<UBlueprintFactory>();
	Factory->ParentClass = ParentClass;

	UBlueprint* NewBP = Cast<UBlueprint>(Factory->FactoryCreateNew(
		UBlueprint::StaticClass(), Package, FName(*AssetName),
		RF_Public | RF_Standalone, nullptr, GWarn));

	if (!NewBP)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Failed to create %s Blueprint"), *TypeLabel));
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(NewBP);
	FKismetEditorUtilities::CompileBlueprint(NewBP);

	FAssetRegistryModule::AssetCreated(NewBP);
	Package->MarkPackageDirty();

	FString PackageFilename = FPackageName::LongPackageNameToFilename(PackagePath, FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	UPackage::SavePackage(Package, NewBP, *PackageFilename, SaveArgs);

	return FMCPToolResult::Success(FString::Printf(TEXT("Created %s Blueprint '%s' (parent: %s) at %s"),
		*TypeLabel, *AssetName, *ParentClass->GetName(), *AssetPath));
}

// ============================================================================
// Helper: Get class name safely
// ============================================================================

static FString SafeClassName(UClass* Class)
{
	return Class ? Class->GetPathName() : TEXT("None");
}

// ============================================================================
// RegisterAll
// ============================================================================

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// create_game_mode - Create a GameModeBase Blueprint
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Content path for the new GameMode Blueprint (e.g., '/Game/Blueprints/BP_MyGameMode')"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("display_name"), TEXT("Optional display name for the Blueprint"));
		FMCPSchemaBuilder::AddString(Schema, TEXT("default_pawn_class"), TEXT("Content path to the default pawn class Blueprint (e.g., '/Game/Blueprints/BP_MyPawn')"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("create_game_mode");
		Def.Description = TEXT("Create a new GameModeBase Blueprint. Optionally set the default pawn class. The GameMode controls match rules, spawning, and game flow.");
		Def.InputSchema = Schema;
		Def.bReadOnlyHint = false;
		Def.bDestructiveHint = false;
		Def.bIdempotentHint = true;
		Def.bOpenWorldHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));

			FMCPToolResult Result = CreateFrameworkBlueprint(AssetPath, AGameModeBase::StaticClass(), TEXT("GameMode"));
			if (Result.bIsError)
				return Result;

			// If default_pawn_class was specified, attempt to set it as a CDO property
			FString DefaultPawnPath;
			if (Args->TryGetStringField(TEXT("default_pawn_class"), DefaultPawnPath) && !DefaultPawnPath.IsEmpty())
			{
				UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *AssetPath);
				if (BP && BP->GeneratedClass)
				{
					AGameModeBase* CDO = Cast<AGameModeBase>(BP->GeneratedClass->GetDefaultObject());
					if (CDO)
					{
						UClass* PawnClass = LoadObject<UClass>(nullptr, *DefaultPawnPath);
						if (!PawnClass)
						{
							// Try loading as Blueprint and getting generated class
							UBlueprint* PawnBP = LoadObject<UBlueprint>(nullptr, *DefaultPawnPath);
							if (PawnBP)
								PawnClass = PawnBP->GeneratedClass;
						}
						if (PawnClass)
						{
							CDO->DefaultPawnClass = PawnClass;
							FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
							FKismetEditorUtilities::CompileBlueprint(BP);

							FString PackagePath = FPackageName::ObjectPathToPackageName(AssetPath);
							FString PackageFilename = FPackageName::LongPackageNameToFilename(PackagePath, FPackageName::GetAssetPackageExtension());
							FSavePackageArgs SaveArgs;
							SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
							UPackage::SavePackage(BP->GetOutermost(), BP, *PackageFilename, SaveArgs);
						}
					}
				}
			}

			return Result;
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// create_player_controller - Create a PlayerController Blueprint
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Content path for the new PlayerController Blueprint (e.g., '/Game/Blueprints/BP_MyPC')"), true);

		FMCPToolDefinition Def;
		Def.Name = TEXT("create_player_controller");
		Def.Description = TEXT("Create a new PlayerController Blueprint. The PlayerController handles player input, camera management, and HUD interaction.");
		Def.InputSchema = Schema;
		Def.bReadOnlyHint = false;
		Def.bDestructiveHint = false;
		Def.bIdempotentHint = true;
		Def.bOpenWorldHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));

			return CreateFrameworkBlueprint(AssetPath, APlayerController::StaticClass(), TEXT("PlayerController"));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// create_game_state - Create a GameStateBase Blueprint
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Content path for the new GameState Blueprint (e.g., '/Game/Blueprints/BP_MyGameState')"), true);

		FMCPToolDefinition Def;
		Def.Name = TEXT("create_game_state");
		Def.Description = TEXT("Create a new GameStateBase Blueprint. The GameState holds replicated game-wide state such as scores, match timers, and team info.");
		Def.InputSchema = Schema;
		Def.bReadOnlyHint = false;
		Def.bDestructiveHint = false;
		Def.bIdempotentHint = true;
		Def.bOpenWorldHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));

			return CreateFrameworkBlueprint(AssetPath, AGameStateBase::StaticClass(), TEXT("GameState"));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// create_player_state - Create a PlayerState Blueprint
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Content path for the new PlayerState Blueprint (e.g., '/Game/Blueprints/BP_MyPlayerState')"), true);

		FMCPToolDefinition Def;
		Def.Name = TEXT("create_player_state");
		Def.Description = TEXT("Create a new PlayerState Blueprint. The PlayerState holds replicated per-player data such as player name, score, and team assignment.");
		Def.InputSchema = Schema;
		Def.bReadOnlyHint = false;
		Def.bDestructiveHint = false;
		Def.bIdempotentHint = true;
		Def.bOpenWorldHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));

			return CreateFrameworkBlueprint(AssetPath, APlayerState::StaticClass(), TEXT("PlayerState"));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// create_hud - Create a HUD Blueprint
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Content path for the new HUD Blueprint (e.g., '/Game/Blueprints/BP_MyHUD')"), true);

		FMCPToolDefinition Def;
		Def.Name = TEXT("create_hud");
		Def.Description = TEXT("Create a new HUD Blueprint. The HUD class handles drawing canvas-based UI elements and managing the player's heads-up display.");
		Def.InputSchema = Schema;
		Def.bReadOnlyHint = false;
		Def.bDestructiveHint = false;
		Def.bIdempotentHint = true;
		Def.bOpenWorldHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));

			return CreateFrameworkBlueprint(AssetPath, AHUD::StaticClass(), TEXT("HUD"));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// get_game_framework_info - Query current game framework setup
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();

		FMCPToolDefinition Def;
		Def.Name = TEXT("get_game_framework_info");
		Def.Description = TEXT("Get the current world's game framework configuration. Returns the GameMode class and its configured default pawn, player controller, player state, HUD, and game state classes.");
		Def.InputSchema = Schema;
		Def.bReadOnlyHint = true;
		Def.bDestructiveHint = false;
		Def.bIdempotentHint = false;
		Def.bOpenWorldHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
			if (!World)
				return FMCPToolResult::Error(TEXT("No editor world available"));

			TSharedPtr<FJsonObject> Info = MakeShared<FJsonObject>();

			// World Settings - configured GameMode class
			AWorldSettings* WS = World->GetWorldSettings();
			if (WS)
			{
				Info->SetStringField(TEXT("worldSettingsGameMode"),
					WS->DefaultGameMode ? WS->DefaultGameMode->GetPathName() : TEXT("None"));
			}

			// Active GameMode instance (if PIE or preview)
			AGameModeBase* GM = World->GetAuthGameMode();
			if (GM)
			{
				TSharedPtr<FJsonObject> GMInfo = MakeShared<FJsonObject>();
				GMInfo->SetStringField(TEXT("class"), GM->GetClass()->GetPathName());
				GMInfo->SetStringField(TEXT("defaultPawnClass"), SafeClassName(GM->DefaultPawnClass));
				GMInfo->SetStringField(TEXT("playerControllerClass"), SafeClassName(GM->PlayerControllerClass));
				GMInfo->SetStringField(TEXT("playerStateClass"), SafeClassName(GM->PlayerStateClass));
				GMInfo->SetStringField(TEXT("hudClass"), SafeClassName(GM->HUDClass));
				GMInfo->SetStringField(TEXT("gameStateClass"), SafeClassName(GM->GameStateClass));
				Info->SetObjectField(TEXT("activeGameMode"), GMInfo);
			}
			else
			{
				// No active game mode (editor, not PIE) - read from world settings default
				if (WS && WS->DefaultGameMode)
				{
					UClass* GMClass = WS->DefaultGameMode;
					AGameModeBase* GMCDO = Cast<AGameModeBase>(GMClass->GetDefaultObject());
					if (GMCDO)
					{
						TSharedPtr<FJsonObject> GMInfo = MakeShared<FJsonObject>();
						GMInfo->SetStringField(TEXT("class"), GMClass->GetPathName());
						GMInfo->SetStringField(TEXT("defaultPawnClass"), SafeClassName(GMCDO->DefaultPawnClass));
						GMInfo->SetStringField(TEXT("playerControllerClass"), SafeClassName(GMCDO->PlayerControllerClass));
						GMInfo->SetStringField(TEXT("playerStateClass"), SafeClassName(GMCDO->PlayerStateClass));
						GMInfo->SetStringField(TEXT("hudClass"), SafeClassName(GMCDO->HUDClass));
						GMInfo->SetStringField(TEXT("gameStateClass"), SafeClassName(GMCDO->GameStateClass));
						Info->SetObjectField(TEXT("configuredGameMode"), GMInfo);
					}
				}
				else
				{
					Info->SetStringField(TEXT("note"), TEXT("No GameMode configured in World Settings. Using engine default."));

					// Show the engine default GameMode CDO info
					AGameModeBase* DefaultCDO = GetMutableDefault<AGameModeBase>();
					if (DefaultCDO)
					{
						TSharedPtr<FJsonObject> GMInfo = MakeShared<FJsonObject>();
						GMInfo->SetStringField(TEXT("class"), AGameModeBase::StaticClass()->GetPathName());
						GMInfo->SetStringField(TEXT("defaultPawnClass"), SafeClassName(DefaultCDO->DefaultPawnClass));
						GMInfo->SetStringField(TEXT("playerControllerClass"), SafeClassName(DefaultCDO->PlayerControllerClass));
						GMInfo->SetStringField(TEXT("playerStateClass"), SafeClassName(DefaultCDO->PlayerStateClass));
						GMInfo->SetStringField(TEXT("hudClass"), SafeClassName(DefaultCDO->HUDClass));
						GMInfo->SetStringField(TEXT("gameStateClass"), SafeClassName(DefaultCDO->GameStateClass));
						Info->SetObjectField(TEXT("engineDefault"), GMInfo);
					}
				}
			}

			return FMCPToolResult::Success(JsonToString(Info));
		});
		Registry.RegisterTool(Def);
	}
}

} // namespace MCPGameFrameworkTools
