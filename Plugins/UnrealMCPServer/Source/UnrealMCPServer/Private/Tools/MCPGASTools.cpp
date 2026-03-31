#include "Tools/MCPGASTools.h"
#include "MCPToolRegistry.h"
#include "MCPProtocol.h"

#if defined(WITH_GAMEPLAY_ABILITIES) && WITH_GAMEPLAY_ABILITIES
#include "AbilitySystemComponent.h"
#include "GameplayAbilitySpec.h"
#include "Abilities/GameplayAbility.h"
#include "GameplayEffect.h"
#include "AttributeSet.h"
#endif

#include "Engine/World.h"
#include "Engine/Blueprint.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Factories/BlueprintFactory.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "FileHelpers.h"

namespace MCPGASTools
{

// ============================================================================
// Helper functions
// ============================================================================

static UWorld* GetEditorWorld()
{
	if (GEditor)
	{
		return GEditor->GetEditorWorldContext().World();
	}
	return nullptr;
}

static AActor* FindActorByLabel(UWorld* World, const FString& Label)
{
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if ((*It)->GetActorLabel() == Label) return *It;
	}
	return nullptr;
}

static UBlueprint* FindBlueprint(const FString& AssetPath)
{
	return LoadObject<UBlueprint>(nullptr, *AssetPath);
}

/**
 * Dynamically find a GAS class by name. Tries multiple lookup strategies:
 * 1. FindFirstObject exact match
 * 2. LoadClass from /Script/GameplayAbilities module
 * 3. FindFirstObject with U prefix
 */
static UClass* FindGASClass(const FString& ClassName)
{
	// Try direct lookup
	UClass* FoundClass = FindFirstObject<UClass>(*ClassName, EFindFirstObjectOptions::ExactClass);
	if (FoundClass) return FoundClass;

	// Try loading from GameplayAbilities module
	FoundClass = LoadClass<UObject>(nullptr, *FString::Printf(TEXT("/Script/GameplayAbilities.%s"), *ClassName));
	if (FoundClass) return FoundClass;

	// Try with U prefix
	FoundClass = FindFirstObject<UClass>(*FString::Printf(TEXT("U%s"), *ClassName), EFindFirstObjectOptions::ExactClass);
	if (FoundClass) return FoundClass;

	return nullptr;
}

/**
 * Creates a Blueprint with a dynamically-resolved parent class.
 * Returns the created Blueprint or nullptr on failure.
 * OutError is populated with an error message on failure.
 */
static UBlueprint* CreateGASBlueprint(const FString& AssetPath, const FString& ParentClassName, FString& OutError)
{
	UClass* ParentClass = FindGASClass(ParentClassName);
	if (!ParentClass)
	{
		OutError = FString::Printf(
			TEXT("Parent class '%s' not found. Ensure the GameplayAbilities plugin is enabled in your project."),
			*ParentClassName);
		return nullptr;
	}

	FString PackagePath = FPackageName::ObjectPathToPackageName(AssetPath);
	FString AssetName = FPackageName::GetShortName(AssetPath);

	UPackage* Package = CreatePackage(*PackagePath);
	if (!Package)
	{
		OutError = FString::Printf(TEXT("Failed to create package: %s"), *PackagePath);
		return nullptr;
	}

	UBlueprintFactory* Factory = NewObject<UBlueprintFactory>();
	Factory->ParentClass = ParentClass;

	UBlueprint* NewBP = Cast<UBlueprint>(Factory->FactoryCreateNew(
		UBlueprint::StaticClass(), Package, FName(*AssetName),
		RF_Public | RF_Standalone, nullptr, GWarn));

	if (!NewBP)
	{
		OutError = FString::Printf(TEXT("Failed to create Blueprint for class '%s'"), *ParentClassName);
		return nullptr;
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(NewBP);
	FKismetEditorUtilities::CompileBlueprint(NewBP);

	FAssetRegistryModule::AssetCreated(NewBP);
	Package->MarkPackageDirty();

	FString PackageFilename = FPackageName::LongPackageNameToFilename(PackagePath, FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	UPackage::SavePackage(Package, NewBP, *PackageFilename, SaveArgs);

	return NewBP;
}

/**
 * Lists Blueprint assets that inherit from a given GAS base class.
 * Uses the Asset Registry to search by path and optionally filter by name substring.
 */
static FMCPToolResult ListGASAssets(const FString& BaseClassName, const FString& Path, const FString& NameFilter)
{
	UClass* BaseClass = FindGASClass(BaseClassName);
	if (!BaseClass)
	{
		return FMCPToolResult::Error(FString::Printf(
			TEXT("Base class '%s' not found. Ensure the GameplayAbilities plugin is enabled."),
			*BaseClassName));
	}

	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AR = ARM.Get();

	TArray<FAssetData> AllAssets;
	AR.GetAssetsByPath(FName(*Path), AllAssets, true);

	TArray<TSharedPtr<FJsonValue>> ResultArray;
	for (const FAssetData& Asset : AllAssets)
	{
		// We only want Blueprint assets
		if (Asset.AssetClassPath != UBlueprint::StaticClass()->GetClassPathName())
			continue;

		// Apply name filter if provided
		if (!NameFilter.IsEmpty() && !Asset.AssetName.ToString().Contains(NameFilter))
			continue;

		// Load the Blueprint to check parent class
		UBlueprint* BP = Cast<UBlueprint>(Asset.GetAsset());
		if (!BP || !BP->GeneratedClass || !BP->GeneratedClass->IsChildOf(BaseClass))
			continue;

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Asset.AssetName.ToString());
		Entry->SetStringField(TEXT("path"), Asset.GetObjectPathString());
		Entry->SetStringField(TEXT("parent_class"), BP->ParentClass ? BP->ParentClass->GetName() : TEXT("Unknown"));
		ResultArray.Add(MakeShared<FJsonValueObject>(Entry));
	}

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetStringField(TEXT("base_class"), BaseClassName);
	ResultObj->SetStringField(TEXT("search_path"), Path);
	ResultObj->SetNumberField(TEXT("count"), ResultArray.Num());
	ResultObj->SetArrayField(TEXT("assets"), ResultArray);

	return FMCPToolResult::Success(JsonToString(ResultObj));
}

// ============================================================================
// RegisterAll
// ============================================================================

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// create_gameplay_ability - Create a GameplayAbility Blueprint
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"),
			TEXT("Content path for the new ability Blueprint (e.g., '/Game/Abilities/GA_FireBlast')"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("ability_name"),
			TEXT("Display name for the ability (set as the Blueprint's asset name if different from path)"));
		FMCPSchemaBuilder::AddString(Schema, TEXT("parent_class"),
			TEXT("Parent class name (default: 'GameplayAbility'). Can be a custom ability base class."));

		FMCPToolDefinition Def;
		Def.Name = TEXT("create_gameplay_ability");
		Def.Description = TEXT("Create a Blueprint inheriting from GameplayAbility (or a custom subclass). "
			"Requires the GameplayAbilities plugin to be enabled. The parent class is resolved dynamically by name.");
		Def.InputSchema = Schema;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));

			FString ParentClassName = TEXT("GameplayAbility");
			Args->TryGetStringField(TEXT("parent_class"), ParentClassName);

			FString Error;
			UBlueprint* NewBP = CreateGASBlueprint(AssetPath, ParentClassName, Error);
			if (!NewBP)
				return FMCPToolResult::Error(Error);

			FString AbilityName;
			if (Args->TryGetStringField(TEXT("ability_name"), AbilityName) && !AbilityName.IsEmpty())
			{
				// Store ability_name as metadata - the asset name comes from the path
			}

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Created GameplayAbility Blueprint '%s' (parent: %s) at %s"),
				*NewBP->GetName(), *ParentClassName, *AssetPath));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// create_gameplay_effect - Create a GameplayEffect Blueprint
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"),
			TEXT("Content path for the new effect Blueprint (e.g., '/Game/Effects/GE_DamageOverTime')"), true);
		FMCPSchemaBuilder::AddEnum(Schema, TEXT("duration_policy"),
			TEXT("Duration policy for the effect"),
			{TEXT("Instant"), TEXT("Infinite"), TEXT("HasDuration")});

		FMCPToolDefinition Def;
		Def.Name = TEXT("create_gameplay_effect");
		Def.Description = TEXT("Create a Blueprint inheriting from GameplayEffect. "
			"Optionally set the duration policy (Instant, Infinite, or HasDuration). "
			"Requires the GameplayAbilities plugin to be enabled.");
		Def.InputSchema = Schema;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));

			FString DurationPolicy = TEXT("Instant");
			Args->TryGetStringField(TEXT("duration_policy"), DurationPolicy);

			FString Error;
			UBlueprint* NewBP = CreateGASBlueprint(AssetPath, TEXT("GameplayEffect"), Error);
			if (!NewBP)
				return FMCPToolResult::Error(Error);

			// Attempt to set the DurationPolicy CDO property
			if (NewBP->GeneratedClass)
			{
				UObject* CDO = NewBP->GeneratedClass->GetDefaultObject();
				if (CDO)
				{
					FProperty* DurProp = NewBP->GeneratedClass->FindPropertyByName(FName(TEXT("DurationPolicy")));
					if (DurProp)
					{
						// Map string to enum value
						int32 EnumValue = 0; // Instant
						if (DurationPolicy == TEXT("Infinite")) EnumValue = 1;
						else if (DurationPolicy == TEXT("HasDuration")) EnumValue = 2;

						void* ValuePtr = DurProp->ContainerPtrToValuePtr<void>(CDO);
						FNumericProperty* NumProp = CastField<FNumericProperty>(DurProp);
						if (NumProp)
						{
							NumProp->SetIntPropertyValue(ValuePtr, static_cast<int64>(EnumValue));
						}
						else
						{
							// Try import text for enum properties
							DurProp->ImportText_Direct(*DurationPolicy, ValuePtr, CDO, PPF_None);
						}

						FBlueprintEditorUtils::MarkBlueprintAsModified(NewBP);
						FKismetEditorUtilities::CompileBlueprint(NewBP);
					}
				}
			}

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Created GameplayEffect Blueprint '%s' (duration: %s) at %s"),
				*NewBP->GetName(), *DurationPolicy, *AssetPath));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// create_attribute_set - Create an AttributeSet Blueprint
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"),
			TEXT("Content path for the new AttributeSet Blueprint (e.g., '/Game/Attributes/AS_CharacterStats')"), true);
		FMCPSchemaBuilder::AddStringArray(Schema, TEXT("attributes"),
			TEXT("Array of attribute names to create as Float variables (e.g., [\"Health\", \"Mana\", \"Stamina\"])"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("create_attribute_set");
		Def.Description = TEXT("Create a Blueprint inheriting from AttributeSet. "
			"Optionally pre-populate it with named Float variables representing gameplay attributes. "
			"Requires the GameplayAbilities plugin to be enabled.");
		Def.InputSchema = Schema;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));

			FString Error;
			UBlueprint* NewBP = CreateGASBlueprint(AssetPath, TEXT("AttributeSet"), Error);
			if (!NewBP)
				return FMCPToolResult::Error(Error);

			// Add Float variables for each attribute
			TArray<FString> AttributeNames;
			const TArray<TSharedPtr<FJsonValue>>* AttrsArray = nullptr;
			if (Args->TryGetArrayField(TEXT("attributes"), AttrsArray) && AttrsArray)
			{
				for (const TSharedPtr<FJsonValue>& Val : *AttrsArray)
				{
					FString AttrName = Val->AsString();
					if (AttrName.IsEmpty()) continue;
					AttributeNames.Add(AttrName);

					// Create a Float variable on the Blueprint
					FEdGraphPinType PinType;
					PinType.PinCategory = UEdGraphSchema_K2::PC_Real;
					PinType.PinSubCategory = UEdGraphSchema_K2::PC_Double;

					FBlueprintEditorUtils::AddMemberVariable(NewBP, FName(*AttrName), PinType);
				}

				if (AttributeNames.Num() > 0)
				{
					FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(NewBP);
					FKismetEditorUtilities::CompileBlueprint(NewBP);

					// Re-save after adding variables
					FString PackagePath = FPackageName::ObjectPathToPackageName(AssetPath);
					FString PackageFilename = FPackageName::LongPackageNameToFilename(
						PackagePath, FPackageName::GetAssetPackageExtension());
					FSavePackageArgs SaveArgs;
					SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
					UPackage::SavePackage(NewBP->GetPackage(), NewBP, *PackageFilename, SaveArgs);
				}
			}

			FString AttrList = AttributeNames.Num() > 0
				? FString::Join(AttributeNames, TEXT(", "))
				: TEXT("none");

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Created AttributeSet Blueprint '%s' at %s with attributes: [%s]"),
				*NewBP->GetName(), *AssetPath, *AttrList));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// list_gameplay_abilities - List all GameplayAbility assets
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("path"),
			TEXT("Content path to search (e.g., '/Game/'). Default: '/Game/'"));
		FMCPSchemaBuilder::AddString(Schema, TEXT("name_filter"),
			TEXT("Filter by asset name (substring match, case-insensitive)"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("list_gameplay_abilities");
		Def.Description = TEXT("List all Blueprint assets that inherit from GameplayAbility. "
			"Searches the Asset Registry under the specified path. "
			"Requires the GameplayAbilities plugin to be enabled.");
		Def.InputSchema = Schema;
		Def.bReadOnlyHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString Path = TEXT("/Game/");
			Args->TryGetStringField(TEXT("path"), Path);

			FString NameFilter;
			Args->TryGetStringField(TEXT("name_filter"), NameFilter);

			return ListGASAssets(TEXT("GameplayAbility"), Path, NameFilter);
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// list_gameplay_effects - List all GameplayEffect assets
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("path"),
			TEXT("Content path to search (e.g., '/Game/'). Default: '/Game/'"));
		FMCPSchemaBuilder::AddString(Schema, TEXT("name_filter"),
			TEXT("Filter by asset name (substring match, case-insensitive)"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("list_gameplay_effects");
		Def.Description = TEXT("List all Blueprint assets that inherit from GameplayEffect. "
			"Searches the Asset Registry under the specified path. "
			"Requires the GameplayAbilities plugin to be enabled.");
		Def.InputSchema = Schema;
		Def.bReadOnlyHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString Path = TEXT("/Game/");
			Args->TryGetStringField(TEXT("path"), Path);

			FString NameFilter;
			Args->TryGetStringField(TEXT("name_filter"), NameFilter);

			return ListGASAssets(TEXT("GameplayEffect"), Path, NameFilter);
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// list_attribute_sets - List all AttributeSet assets
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("path"),
			TEXT("Content path to search (e.g., '/Game/'). Default: '/Game/'"));
		FMCPSchemaBuilder::AddString(Schema, TEXT("name_filter"),
			TEXT("Filter by asset name (substring match, case-insensitive)"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("list_attribute_sets");
		Def.Description = TEXT("List all Blueprint assets that inherit from AttributeSet. "
			"Searches the Asset Registry under the specified path. "
			"Requires the GameplayAbilities plugin to be enabled.");
		Def.InputSchema = Schema;
		Def.bReadOnlyHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString Path = TEXT("/Game/");
			Args->TryGetStringField(TEXT("path"), Path);

			FString NameFilter;
			Args->TryGetStringField(TEXT("name_filter"), NameFilter);

			return ListGASAssets(TEXT("AttributeSet"), Path, NameFilter);
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// add_ability_component - Add AbilitySystemComponent to a Blueprint
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"),
			TEXT("Content path of the target Blueprint to add the AbilitySystemComponent to (e.g., '/Game/Blueprints/BP_MyCharacter')"), true);

		FMCPToolDefinition Def;
		Def.Name = TEXT("add_ability_component");
		Def.Description = TEXT("Add an AbilitySystemComponent to a Blueprint's component hierarchy (SCS). "
			"The component class is resolved dynamically by name from the GameplayAbilities module. "
			"Requires the GameplayAbilities plugin to be enabled.");
		Def.InputSchema = Schema;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));

			UBlueprint* BP = FindBlueprint(AssetPath);
			if (!BP)
				return FMCPToolResult::Error(FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));

			if (!BP->SimpleConstructionScript)
				return FMCPToolResult::Error(TEXT("Blueprint has no Simple Construction Script (not an Actor-based BP?)"));

			// Check if an AbilitySystemComponent already exists
			for (USCS_Node* Node : BP->SimpleConstructionScript->GetAllNodes())
			{
				if (Node && Node->ComponentTemplate)
				{
					UClass* CompClass = Node->ComponentTemplate->GetClass();
					if (CompClass->GetName() == TEXT("AbilitySystemComponent") ||
						CompClass->IsChildOf(FindGASClass(TEXT("AbilitySystemComponent"))))
					{
						return FMCPToolResult::Success(FString::Printf(
							TEXT("Blueprint '%s' already has an AbilitySystemComponent ('%s')"),
							*BP->GetName(), *Node->GetVariableName().ToString()));
					}
				}
			}

			// Find AbilitySystemComponent class dynamically
			UClass* ASCClass = FindGASClass(TEXT("AbilitySystemComponent"));
			if (!ASCClass)
			{
				return FMCPToolResult::Error(
					TEXT("AbilitySystemComponent class not found. Ensure the GameplayAbilities plugin is enabled."));
			}

			if (!ASCClass->IsChildOf(UActorComponent::StaticClass()))
			{
				return FMCPToolResult::Error(
					TEXT("AbilitySystemComponent resolved but is not an ActorComponent subclass (unexpected)."));
			}

			FName CompName(TEXT("AbilitySystemComponent"));
			USCS_Node* NewNode = BP->SimpleConstructionScript->CreateNode(ASCClass, CompName);
			if (!NewNode)
			{
				return FMCPToolResult::Error(TEXT("Failed to create AbilitySystemComponent SCS node"));
			}

			BP->SimpleConstructionScript->AddNode(NewNode);
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Added AbilitySystemComponent '%s' to Blueprint '%s'"),
				*NewNode->GetVariableName().ToString(), *BP->GetName()));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// get_gas_info - Get GAS setup info for an actor in the level
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("actor_name"),
			TEXT("Label of the actor in the current level to inspect for GAS setup"), true);

		FMCPToolDefinition Def;
		Def.Name = TEXT("get_gas_info");
		Def.Description = TEXT("Get Gameplay Ability System setup information for an actor in the current level. "
			"Reports whether the actor has an AbilitySystemComponent, lists granted abilities, "
			"and active gameplay effects. Requires the GameplayAbilities plugin to be enabled.");
		Def.InputSchema = Schema;
		Def.bReadOnlyHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString ActorName;
			if (!Args->TryGetStringField(TEXT("actor_name"), ActorName))
				return FMCPToolResult::Error(TEXT("actor_name is required"));

			UWorld* World = GetEditorWorld();
			if (!World)
				return FMCPToolResult::Error(TEXT("No editor world available"));

			AActor* Actor = FindActorByLabel(World, ActorName);
			if (!Actor)
				return FMCPToolResult::Error(FString::Printf(TEXT("Actor not found: %s"), *ActorName));

			TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
			ResultObj->SetStringField(TEXT("actor_name"), ActorName);
			ResultObj->SetStringField(TEXT("actor_class"), Actor->GetClass()->GetName());

			// Find AbilitySystemComponent by class name (dynamic lookup)
			UActorComponent* ASComp = nullptr;
			UClass* ASCClass = FindGASClass(TEXT("AbilitySystemComponent"));

			if (ASCClass)
			{
				TArray<UActorComponent*> Components;
				Actor->GetComponents(Components);
				for (UActorComponent* Comp : Components)
				{
					if (Comp && Comp->GetClass()->IsChildOf(ASCClass))
					{
						ASComp = Comp;
						break;
					}
				}
			}

			bool bHasAbilitySystem = (ASComp != nullptr);
			ResultObj->SetBoolField(TEXT("has_ability_system"), bHasAbilitySystem);

			if (!bHasAbilitySystem)
			{
				if (!ASCClass)
				{
					ResultObj->SetStringField(TEXT("note"),
						TEXT("GameplayAbilities plugin may not be enabled - AbilitySystemComponent class not found."));
				}
				else
				{
					ResultObj->SetStringField(TEXT("note"),
						TEXT("Actor does not have an AbilitySystemComponent."));
				}

				ResultObj->SetArrayField(TEXT("granted_abilities"), TArray<TSharedPtr<FJsonValue>>());
				ResultObj->SetArrayField(TEXT("active_effects"), TArray<TSharedPtr<FJsonValue>>());

				return FMCPToolResult::Success(JsonToString(ResultObj));
			}

			// Gather ability and effect info using reflection (avoids hard dependency)
			TArray<TSharedPtr<FJsonValue>> AbilitiesArray;
			TArray<TSharedPtr<FJsonValue>> EffectsArray;

#if defined(WITH_GAMEPLAY_ABILITIES) && WITH_GAMEPLAY_ABILITIES
			// When compiled with GAS support, use typed access
			UAbilitySystemComponent* ASC = Cast<UAbilitySystemComponent>(ASComp);
			if (ASC)
			{
				// List granted abilities
				const TArray<FGameplayAbilitySpec>& Specs = ASC->GetActivatableAbilities();
				for (const FGameplayAbilitySpec& Spec : Specs)
				{
					TSharedPtr<FJsonObject> AbilityObj = MakeShared<FJsonObject>();
					if (Spec.Ability)
					{
						AbilityObj->SetStringField(TEXT("ability_class"), Spec.Ability->GetClass()->GetName());
						AbilityObj->SetStringField(TEXT("ability_name"), Spec.Ability->GetName());
					}
					AbilityObj->SetNumberField(TEXT("level"), Spec.Level);
					AbilityObj->SetBoolField(TEXT("is_active"), Spec.IsActive());
					AbilityObj->SetNumberField(TEXT("input_id"), Spec.InputID);
					AbilitiesArray.Add(MakeShared<FJsonValueObject>(AbilityObj));
				}

				// List active gameplay effects
				const FActiveGameplayEffectsContainer& ActiveEffects = ASC->GetActiveGameplayEffects();
				for (const FActiveGameplayEffect& Effect : &ActiveEffects)
				{
					TSharedPtr<FJsonObject> EffectObj = MakeShared<FJsonObject>();
					if (Effect.Spec.Def)
					{
						EffectObj->SetStringField(TEXT("effect_class"), Effect.Spec.Def->GetClass()->GetName());
						EffectObj->SetStringField(TEXT("effect_name"), Effect.Spec.Def->GetName());
					}
					EffectObj->SetNumberField(TEXT("level"), Effect.Spec.GetLevel());
					EffectObj->SetNumberField(TEXT("stack_count"), Effect.Spec.GetStackCount());
					EffectsArray.Add(MakeShared<FJsonValueObject>(EffectObj));
				}
			}
#else
			// Without GAS compiled in, report what we can via reflection
			ResultObj->SetStringField(TEXT("component_class"), ASComp->GetClass()->GetName());
			ResultObj->SetStringField(TEXT("reflection_note"),
				TEXT("GameplayAbilities module not compiled in. Component found but detailed ability/effect info unavailable. "
				     "Use the Python bridge tool to query GAS state via UE Python API for full details."));
#endif

			ResultObj->SetArrayField(TEXT("granted_abilities"), AbilitiesArray);
			ResultObj->SetArrayField(TEXT("active_effects"), EffectsArray);

			return FMCPToolResult::Success(JsonToString(ResultObj));
		});
		Registry.RegisterTool(Def);
	}
}

} // namespace MCPGASTools
