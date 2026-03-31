#include "Tools/MCPEnhancedInputTools.h"
#include "MCPToolRegistry.h"
#include "MCPProtocol.h"

#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputActionValue.h"
#include "AssetToolsModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "Factories/DataAssetFactory.h"

namespace MCPEnhancedInputTools
{

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// create_input_action - Create a UInputAction asset
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Content path for the new InputAction (e.g., '/Game/Input/IA_Jump')"), true);
		FMCPSchemaBuilder::AddEnum(Schema, TEXT("value_type"), TEXT("The value type for this action"),
			{TEXT("Bool"), TEXT("Axis1D"), TEXT("Axis2D"), TEXT("Axis3D")}, true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("description"), TEXT("Optional description for the input action"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("create_input_action");
		Def.Description = TEXT("Create a UInputAction asset with a specified value type (Bool, Axis1D, Axis2D, Axis3D).");
		Def.InputSchema = Schema;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));

			FString ValueTypeStr;
			if (!Args->TryGetStringField(TEXT("value_type"), ValueTypeStr))
				return FMCPToolResult::Error(TEXT("value_type is required"));

			// Parse value type
			EInputActionValueType ValueType = EInputActionValueType::Boolean;
			if (ValueTypeStr == TEXT("Bool"))
				ValueType = EInputActionValueType::Boolean;
			else if (ValueTypeStr == TEXT("Axis1D"))
				ValueType = EInputActionValueType::Axis1D;
			else if (ValueTypeStr == TEXT("Axis2D"))
				ValueType = EInputActionValueType::Axis2D;
			else if (ValueTypeStr == TEXT("Axis3D"))
				ValueType = EInputActionValueType::Axis3D;
			else
				return FMCPToolResult::Error(FString::Printf(TEXT("Invalid value_type '%s'. Must be Bool, Axis1D, Axis2D, or Axis3D."), *ValueTypeStr));

			// Create the package and asset
			FString PackagePath = FPackageName::ObjectPathToPackageName(AssetPath);
			UPackage* Package = CreatePackage(*PackagePath);
			if (!Package)
				return FMCPToolResult::Error(FString::Printf(TEXT("Failed to create package: %s"), *PackagePath));

			FString AssetName = FPackageName::GetShortName(AssetPath);
			UInputAction* NewAction = NewObject<UInputAction>(Package, FName(*AssetName), RF_Public | RF_Standalone);
			if (!NewAction)
				return FMCPToolResult::Error(TEXT("Failed to create UInputAction object"));

			NewAction->ValueType = ValueType;

			// Set description if provided
			FString Description;
			if (Args->TryGetStringField(TEXT("description"), Description))
			{
				NewAction->ActionDescription = FText::FromString(Description);
			}

			FAssetRegistryModule::AssetCreated(NewAction);
			Package->MarkPackageDirty();

			// Save the asset
			FString PackageFileName = FPackageName::LongPackageNameToFilename(PackagePath, FPackageName::GetAssetPackageExtension());
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
			UPackage::SavePackage(Package, NewAction, *PackageFileName, SaveArgs);

			return FMCPToolResult::Success(FString::Printf(TEXT("Created InputAction '%s' with ValueType=%s at %s"),
				*AssetName, *ValueTypeStr, *AssetPath));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// create_input_mapping_context - Create a UInputMappingContext asset
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Content path for the new InputMappingContext (e.g., '/Game/Input/IMC_Default')"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("description"), TEXT("Optional description for the mapping context"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("create_input_mapping_context");
		Def.Description = TEXT("Create a UInputMappingContext asset for binding input actions to keys.");
		Def.InputSchema = Schema;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));

			FString PackagePath = FPackageName::ObjectPathToPackageName(AssetPath);
			UPackage* Package = CreatePackage(*PackagePath);
			if (!Package)
				return FMCPToolResult::Error(FString::Printf(TEXT("Failed to create package: %s"), *PackagePath));

			FString AssetName = FPackageName::GetShortName(AssetPath);
			UInputMappingContext* NewIMC = NewObject<UInputMappingContext>(Package, FName(*AssetName), RF_Public | RF_Standalone);
			if (!NewIMC)
				return FMCPToolResult::Error(TEXT("Failed to create UInputMappingContext object"));

			FAssetRegistryModule::AssetCreated(NewIMC);
			Package->MarkPackageDirty();

			// Save the asset
			FString PackageFileName = FPackageName::LongPackageNameToFilename(PackagePath, FPackageName::GetAssetPackageExtension());
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
			UPackage::SavePackage(Package, NewIMC, *PackageFileName, SaveArgs);

			return FMCPToolResult::Success(FString::Printf(TEXT("Created InputMappingContext '%s' at %s"),
				*AssetName, *AssetPath));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// add_action_mapping - Add a key mapping to a mapping context
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("mapping_context_path"), TEXT("Content path of the InputMappingContext asset"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("action_path"), TEXT("Content path of the InputAction asset to bind"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("key"), TEXT("Key name (e.g., 'W', 'SpaceBar', 'LeftMouseButton', 'Gamepad_LeftStick_X')"), true);

		FMCPToolDefinition Def;
		Def.Name = TEXT("add_action_mapping");
		Def.Description = TEXT("Add a key mapping to a UInputMappingContext, binding an InputAction to a specific key.");
		Def.InputSchema = Schema;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString MappingContextPath, ActionPath, KeyName;
			if (!Args->TryGetStringField(TEXT("mapping_context_path"), MappingContextPath))
				return FMCPToolResult::Error(TEXT("mapping_context_path is required"));
			if (!Args->TryGetStringField(TEXT("action_path"), ActionPath))
				return FMCPToolResult::Error(TEXT("action_path is required"));
			if (!Args->TryGetStringField(TEXT("key"), KeyName))
				return FMCPToolResult::Error(TEXT("key is required"));

			// Load the mapping context
			UInputMappingContext* IMC = Cast<UInputMappingContext>(StaticLoadObject(UInputMappingContext::StaticClass(), nullptr, *MappingContextPath));
			if (!IMC)
				return FMCPToolResult::Error(FString::Printf(TEXT("InputMappingContext not found: %s"), *MappingContextPath));

			// Load the input action
			UInputAction* IA = Cast<UInputAction>(StaticLoadObject(UInputAction::StaticClass(), nullptr, *ActionPath));
			if (!IA)
				return FMCPToolResult::Error(FString::Printf(TEXT("InputAction not found: %s"), *ActionPath));

			// Validate key name
			FKey Key(*KeyName);
			if (!Key.IsValid())
				return FMCPToolResult::Error(FString::Printf(TEXT("Invalid key name: '%s'"), *KeyName));

			// Add the mapping
			FEnhancedActionKeyMapping& Mapping = IMC->MapKey(IA, Key);

			IMC->GetOutermost()->MarkPackageDirty();

			// Save the mapping context
			FString PackagePath = IMC->GetOutermost()->GetName();
			FString PackageFileName = FPackageName::LongPackageNameToFilename(PackagePath, FPackageName::GetAssetPackageExtension());
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
			UPackage::SavePackage(IMC->GetOutermost(), IMC, *PackageFileName, SaveArgs);

			return FMCPToolResult::Success(FString::Printf(TEXT("Mapped action '%s' to key '%s' in context '%s'"),
				*IA->GetName(), *KeyName, *IMC->GetName()));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// list_input_actions - List all UInputAction assets
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("path"), TEXT("Content path to search (default: '/Game/')"));
		FMCPSchemaBuilder::AddString(Schema, TEXT("name_filter"), TEXT("Filter by asset name (substring match)"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("list_input_actions");
		Def.Description = TEXT("List all UInputAction assets in the project. Returns asset names, paths, and value types.");
		Def.InputSchema = Schema;
		Def.bReadOnlyHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString Path = TEXT("/Game/");
			Args->TryGetStringField(TEXT("path"), Path);

			FString NameFilter;
			Args->TryGetStringField(TEXT("name_filter"), NameFilter);

			FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
			IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

			TArray<FAssetData> Assets;
			AssetRegistry.GetAssetsByClass(UInputAction::StaticClass()->GetClassPathName(), Assets, true);

			TArray<TSharedPtr<FJsonValue>> Results;
			for (const FAssetData& Asset : Assets)
			{
				if (!Asset.PackagePath.ToString().StartsWith(Path))
					continue;

				if (!NameFilter.IsEmpty() && !Asset.AssetName.ToString().Contains(NameFilter))
					continue;

				TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("name"), Asset.AssetName.ToString());
				Entry->SetStringField(TEXT("path"), Asset.GetObjectPathString());

				// Load to get value type
				UInputAction* IA = Cast<UInputAction>(Asset.GetAsset());
				if (IA)
				{
					FString ValueTypeStr;
					switch (IA->ValueType)
					{
					case EInputActionValueType::Boolean: ValueTypeStr = TEXT("Bool"); break;
					case EInputActionValueType::Axis1D:  ValueTypeStr = TEXT("Axis1D"); break;
					case EInputActionValueType::Axis2D:  ValueTypeStr = TEXT("Axis2D"); break;
					case EInputActionValueType::Axis3D:  ValueTypeStr = TEXT("Axis3D"); break;
					default: ValueTypeStr = TEXT("Unknown"); break;
					}
					Entry->SetStringField(TEXT("value_type"), ValueTypeStr);
				}

				Results.Add(MakeShared<FJsonValueObject>(Entry));
			}

			TSharedPtr<FJsonObject> Output = MakeShared<FJsonObject>();
			Output->SetNumberField(TEXT("count"), Results.Num());
			Output->SetArrayField(TEXT("input_actions"), Results);

			return FMCPToolResult::Success(JsonToString(Output));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// list_input_mapping_contexts - List all UInputMappingContext assets
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("path"), TEXT("Content path to search (default: '/Game/')"));
		FMCPSchemaBuilder::AddString(Schema, TEXT("name_filter"), TEXT("Filter by asset name (substring match)"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("list_input_mapping_contexts");
		Def.Description = TEXT("List all UInputMappingContext assets in the project. Returns asset names and paths.");
		Def.InputSchema = Schema;
		Def.bReadOnlyHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString Path = TEXT("/Game/");
			Args->TryGetStringField(TEXT("path"), Path);

			FString NameFilter;
			Args->TryGetStringField(TEXT("name_filter"), NameFilter);

			FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
			IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

			TArray<FAssetData> Assets;
			AssetRegistry.GetAssetsByClass(UInputMappingContext::StaticClass()->GetClassPathName(), Assets, true);

			TArray<TSharedPtr<FJsonValue>> Results;
			for (const FAssetData& Asset : Assets)
			{
				if (!Asset.PackagePath.ToString().StartsWith(Path))
					continue;

				if (!NameFilter.IsEmpty() && !Asset.AssetName.ToString().Contains(NameFilter))
					continue;

				TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("name"), Asset.AssetName.ToString());
				Entry->SetStringField(TEXT("path"), Asset.GetObjectPathString());

				// Load to get mapping count
				UInputMappingContext* IMC = Cast<UInputMappingContext>(Asset.GetAsset());
				if (IMC)
				{
					Entry->SetNumberField(TEXT("mapping_count"), IMC->GetMappings().Num());
				}

				Results.Add(MakeShared<FJsonValueObject>(Entry));
			}

			TSharedPtr<FJsonObject> Output = MakeShared<FJsonObject>();
			Output->SetNumberField(TEXT("count"), Results.Num());
			Output->SetArrayField(TEXT("mapping_contexts"), Results);

			return FMCPToolResult::Success(JsonToString(Output));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// get_input_mapping_info - Get all bindings in a mapping context
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("mapping_context_path"), TEXT("Content path of the InputMappingContext asset"), true);

		FMCPToolDefinition Def;
		Def.Name = TEXT("get_input_mapping_info");
		Def.Description = TEXT("Get all key bindings in a UInputMappingContext. Returns action names, keys, modifiers, and triggers for each mapping.");
		Def.InputSchema = Schema;
		Def.bReadOnlyHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString MappingContextPath;
			if (!Args->TryGetStringField(TEXT("mapping_context_path"), MappingContextPath))
				return FMCPToolResult::Error(TEXT("mapping_context_path is required"));

			UInputMappingContext* IMC = Cast<UInputMappingContext>(StaticLoadObject(UInputMappingContext::StaticClass(), nullptr, *MappingContextPath));
			if (!IMC)
				return FMCPToolResult::Error(FString::Printf(TEXT("InputMappingContext not found: %s"), *MappingContextPath));

			const TArray<FEnhancedActionKeyMapping>& Mappings = IMC->GetMappings();

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("name"), IMC->GetName());
			Result->SetStringField(TEXT("path"), IMC->GetPathName());
			Result->SetNumberField(TEXT("mapping_count"), Mappings.Num());

			TArray<TSharedPtr<FJsonValue>> MappingArray;
			for (const FEnhancedActionKeyMapping& Mapping : Mappings)
			{
				TSharedPtr<FJsonObject> MappingObj = MakeShared<FJsonObject>();

				// Action name
				if (Mapping.Action)
				{
					MappingObj->SetStringField(TEXT("action_name"), Mapping.Action->GetName());
					MappingObj->SetStringField(TEXT("action_path"), Mapping.Action->GetPathName());
				}
				else
				{
					MappingObj->SetStringField(TEXT("action_name"), TEXT("(none)"));
				}

				// Key
				MappingObj->SetStringField(TEXT("key"), Mapping.Key.GetFName().ToString());

				// Modifiers
				TArray<TSharedPtr<FJsonValue>> ModifiersArray;
				for (UInputModifier* Modifier : Mapping.Modifiers)
				{
					if (Modifier)
					{
						ModifiersArray.Add(MakeShared<FJsonValueString>(Modifier->GetClass()->GetName()));
					}
				}
				MappingObj->SetArrayField(TEXT("modifiers"), ModifiersArray);

				// Triggers
				TArray<TSharedPtr<FJsonValue>> TriggersArray;
				for (UInputTrigger* Trigger : Mapping.Triggers)
				{
					if (Trigger)
					{
						TriggersArray.Add(MakeShared<FJsonValueString>(Trigger->GetClass()->GetName()));
					}
				}
				MappingObj->SetArrayField(TEXT("triggers"), TriggersArray);

				MappingArray.Add(MakeShared<FJsonValueObject>(MappingObj));
			}

			Result->SetArrayField(TEXT("mappings"), MappingArray);

			return FMCPToolResult::Success(JsonToString(Result));
		});
		Registry.RegisterTool(Def);
	}
}

} // namespace MCPEnhancedInputTools
