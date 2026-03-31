#include "Tools/MCPAITools.h"
#include "MCPToolRegistry.h"
#include "MCPProtocol.h"

#include "Engine/World.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Bool.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Float.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Int.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_String.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Vector.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "EnvironmentQuery/EnvQuery.h"
#include "AssetToolsModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"

namespace MCPAITools
{

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// create_behavior_tree - Create a new BehaviorTree asset
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Content path for the new behavior tree (e.g., '/Game/AI/BT_MyTree')"), true);

		FMCPToolDefinition Def;
		Def.Name = TEXT("create_behavior_tree");
		Def.Description = TEXT("Create a new BehaviorTree asset at the specified content path. The tree is saved empty and ready for editing.");
		Def.InputSchema = Schema;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));

			FString PackagePath = FPackageName::ObjectPathToPackageName(AssetPath);
			FString AssetName = FPackageName::GetShortName(AssetPath);

			UPackage* Package = CreatePackage(*PackagePath);
			if (!Package) return FMCPToolResult::Error(TEXT("Failed to create package"));

			UBehaviorTree* NewBT = NewObject<UBehaviorTree>(Package, FName(*AssetName), RF_Public | RF_Standalone);
			if (!NewBT) return FMCPToolResult::Error(TEXT("Failed to create BehaviorTree object"));

			FAssetRegistryModule::AssetCreated(NewBT);
			Package->MarkPackageDirty();

			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
			FString FilePath = FPackageName::LongPackageNameToFilename(PackagePath, FPackageName::GetAssetPackageExtension());
			UPackage::SavePackage(Package, NewBT, *FilePath, SaveArgs);

			return FMCPToolResult::Success(FString::Printf(TEXT("Created BehaviorTree asset: %s"), *AssetPath));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// create_blackboard - Create a new BlackboardData asset
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Content path for the new blackboard (e.g., '/Game/AI/BB_MyBlackboard')"), true);

		FMCPToolDefinition Def;
		Def.Name = TEXT("create_blackboard");
		Def.Description = TEXT("Create a new BlackboardData asset at the specified content path. The blackboard is saved empty and ready for key editing.");
		Def.InputSchema = Schema;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));

			FString PackagePath = FPackageName::ObjectPathToPackageName(AssetPath);
			FString AssetName = FPackageName::GetShortName(AssetPath);

			UPackage* Package = CreatePackage(*PackagePath);
			if (!Package) return FMCPToolResult::Error(TEXT("Failed to create package"));

			UBlackboardData* NewBB = NewObject<UBlackboardData>(Package, FName(*AssetName), RF_Public | RF_Standalone);
			if (!NewBB) return FMCPToolResult::Error(TEXT("Failed to create BlackboardData object"));

			FAssetRegistryModule::AssetCreated(NewBB);
			Package->MarkPackageDirty();

			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
			FString FilePath = FPackageName::LongPackageNameToFilename(PackagePath, FPackageName::GetAssetPackageExtension());
			UPackage::SavePackage(Package, NewBB, *FilePath, SaveArgs);

			return FMCPToolResult::Success(FString::Printf(TEXT("Created BlackboardData asset: %s"), *AssetPath));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// add_blackboard_key - Add a key to a blackboard
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("blackboard_path"), TEXT("Content path to the BlackboardData asset"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("key_name"), TEXT("Name of the key to add"), true);
		FMCPSchemaBuilder::AddEnum(Schema, TEXT("key_type"), TEXT("Type of the blackboard key"),
			{ TEXT("Bool"), TEXT("Float"), TEXT("Int"), TEXT("String"), TEXT("Vector"), TEXT("Object") }, true);

		FMCPToolDefinition Def;
		Def.Name = TEXT("add_blackboard_key");
		Def.Description = TEXT("Add a key to an existing BlackboardData asset. Supports Bool, Float, Int, String, Vector, and Object key types.");
		Def.InputSchema = Schema;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString BBPath, KeyName, KeyType;
			if (!Args->TryGetStringField(TEXT("blackboard_path"), BBPath))
				return FMCPToolResult::Error(TEXT("blackboard_path is required"));
			if (!Args->TryGetStringField(TEXT("key_name"), KeyName))
				return FMCPToolResult::Error(TEXT("key_name is required"));
			if (!Args->TryGetStringField(TEXT("key_type"), KeyType))
				return FMCPToolResult::Error(TEXT("key_type is required"));

			UBlackboardData* BB = Cast<UBlackboardData>(StaticLoadObject(UBlackboardData::StaticClass(), nullptr, *BBPath));
			if (!BB) return FMCPToolResult::Error(FString::Printf(TEXT("BlackboardData not found: %s"), *BBPath));

			// Check if key already exists
			for (const FBlackboardEntry& Entry : BB->Keys)
			{
				if (Entry.EntryName == FName(*KeyName))
				{
					return FMCPToolResult::Success(FString::Printf(TEXT("Key '%s' already exists on blackboard '%s'"), *KeyName, *BB->GetName()));
				}
			}

			FBlackboardEntry NewEntry;
			NewEntry.EntryName = FName(*KeyName);

			if (KeyType == TEXT("Bool"))
				NewEntry.KeyType = NewObject<UBlackboardKeyType_Bool>(BB);
			else if (KeyType == TEXT("Float"))
				NewEntry.KeyType = NewObject<UBlackboardKeyType_Float>(BB);
			else if (KeyType == TEXT("Int"))
				NewEntry.KeyType = NewObject<UBlackboardKeyType_Int>(BB);
			else if (KeyType == TEXT("String"))
				NewEntry.KeyType = NewObject<UBlackboardKeyType_String>(BB);
			else if (KeyType == TEXT("Vector"))
				NewEntry.KeyType = NewObject<UBlackboardKeyType_Vector>(BB);
			else if (KeyType == TEXT("Object"))
				NewEntry.KeyType = NewObject<UBlackboardKeyType_Object>(BB);
			else
				return FMCPToolResult::Error(FString::Printf(TEXT("Unknown key type: %s. Must be Bool, Float, Int, String, Vector, or Object."), *KeyType));

			BB->Keys.Add(NewEntry);
			BB->MarkPackageDirty();

			return FMCPToolResult::Success(FString::Printf(TEXT("Added key '%s' (type: %s) to blackboard '%s'"), *KeyName, *KeyType, *BB->GetName()));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// get_behavior_tree_info - Returns BT structure info
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Content path of the BehaviorTree asset"), true);

		FMCPToolDefinition Def;
		Def.Name = TEXT("get_behavior_tree_info");
		Def.Description = TEXT("Get information about a BehaviorTree asset: root node class, linked blackboard, and node count.");
		Def.InputSchema = Schema;
		Def.bReadOnlyHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));

			UBehaviorTree* BT = LoadObject<UBehaviorTree>(nullptr, *AssetPath);
			if (!BT) return FMCPToolResult::Error(FString::Printf(TEXT("BehaviorTree not found: %s"), *AssetPath));

			TSharedPtr<FJsonObject> Output = MakeShared<FJsonObject>();
			Output->SetStringField(TEXT("name"), BT->GetName());
			Output->SetStringField(TEXT("path"), AssetPath);

			// Root node
			if (BT->RootNode)
			{
				Output->SetStringField(TEXT("root_node_class"), BT->RootNode->GetClass()->GetName());
			}
			else
			{
				Output->SetStringField(TEXT("root_node_class"), TEXT("None"));
			}

			// Blackboard
			if (BT->BlackboardAsset)
			{
				Output->SetStringField(TEXT("blackboard_asset"), BT->BlackboardAsset->GetPathName());
			}
			else
			{
				Output->SetStringField(TEXT("blackboard_asset"), TEXT("None"));
			}

			// Count nodes in the tree (root + decorators + services recursively)
			int32 NodeCount = 0;
			TArray<UBTNode*> NodesToVisit;
			if (BT->RootNode)
			{
				NodesToVisit.Add(BT->RootNode);
			}
			while (NodesToVisit.Num() > 0)
			{
				UBTNode* Current = NodesToVisit.Pop();
				if (!Current) continue;
				NodeCount++;

				// If it's a composite, add children and count services/decorators
				UBTCompositeNode* Composite = Cast<UBTCompositeNode>(Current);
				if (Composite)
				{
					// Services on the composite node itself
					for (const auto& Service : Composite->Services)
					{
						if (Service) NodeCount++;
					}

					for (int32 i = 0; i < Composite->Children.Num(); i++)
					{
						if (Composite->Children[i].ChildTask)
						{
							UBTTaskNode* Task = Composite->Children[i].ChildTask.Get();
							NodesToVisit.Add(static_cast<UBTNode*>(Task));

							// Services on the task node
							for (const auto& Service : Task->Services)
							{
								if (Service) NodeCount++;
							}
						}
						if (Composite->Children[i].ChildComposite)
						{
							NodesToVisit.Add(static_cast<UBTNode*>(Composite->Children[i].ChildComposite.Get()));
						}

						// Decorators per child
						for (const auto& Decorator : Composite->Children[i].Decorators)
						{
							if (Decorator) NodeCount++;
						}
					}
				}
			}
			Output->SetNumberField(TEXT("node_count"), NodeCount);

			return FMCPToolResult::Success(JsonToString(Output));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// get_blackboard_info - Returns blackboard keys
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Content path of the BlackboardData asset"), true);

		FMCPToolDefinition Def;
		Def.Name = TEXT("get_blackboard_info");
		Def.Description = TEXT("Get information about a BlackboardData asset. Returns all defined keys with their names and types.");
		Def.InputSchema = Schema;
		Def.bReadOnlyHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));

			UBlackboardData* BB = LoadObject<UBlackboardData>(nullptr, *AssetPath);
			if (!BB) return FMCPToolResult::Error(FString::Printf(TEXT("BlackboardData not found: %s"), *AssetPath));

			TSharedPtr<FJsonObject> Output = MakeShared<FJsonObject>();
			Output->SetStringField(TEXT("name"), BB->GetName());
			Output->SetStringField(TEXT("path"), AssetPath);

			// Parent blackboard
			if (BB->Parent)
			{
				Output->SetStringField(TEXT("parent_blackboard"), BB->Parent->GetPathName());
			}

			TArray<TSharedPtr<FJsonValue>> KeysArray;
			for (const FBlackboardEntry& Entry : BB->Keys)
			{
				TSharedPtr<FJsonObject> KeyObj = MakeShared<FJsonObject>();
				KeyObj->SetStringField(TEXT("name"), Entry.EntryName.ToString());

				if (Entry.KeyType)
				{
					KeyObj->SetStringField(TEXT("type"), Entry.KeyType->GetClass()->GetName());
				}
				else
				{
					KeyObj->SetStringField(TEXT("type"), TEXT("Unknown"));
				}

				KeysArray.Add(MakeShared<FJsonValueObject>(KeyObj));
			}

			Output->SetNumberField(TEXT("key_count"), KeysArray.Num());
			Output->SetArrayField(TEXT("keys"), KeysArray);

			return FMCPToolResult::Success(JsonToString(Output));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// list_ai_assets - List BT/Blackboard/EQS assets
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("path"), TEXT("Content path to search (e.g., '/Game/'). Default: '/Game/'"));
		FMCPSchemaBuilder::AddEnum(Schema, TEXT("asset_type"), TEXT("Type of AI assets to list"),
			{ TEXT("BehaviorTree"), TEXT("BlackboardData"), TEXT("EnvQuery"), TEXT("All") });

		FMCPToolDefinition Def;
		Def.Name = TEXT("list_ai_assets");
		Def.Description = TEXT("List AI-related assets (BehaviorTrees, BlackboardData, EnvQuery) found in the project content browser.");
		Def.InputSchema = Schema;
		Def.bReadOnlyHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString Path = TEXT("/Game/");
			Args->TryGetStringField(TEXT("path"), Path);

			FString AssetType = TEXT("All");
			Args->TryGetStringField(TEXT("asset_type"), AssetType);

			FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
			IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

			TSharedPtr<FJsonObject> Output = MakeShared<FJsonObject>();
			int32 TotalCount = 0;

			// Helper lambda to collect assets of a given class
			auto CollectAssets = [&](UClass* AssetClass, const FString& CategoryName)
			{
				TArray<FAssetData> Assets;
				AssetRegistry.GetAssetsByClass(AssetClass->GetClassPathName(), Assets, true);

				TArray<TSharedPtr<FJsonValue>> Results;
				for (const FAssetData& Asset : Assets)
				{
					if (!Asset.PackagePath.ToString().StartsWith(Path))
						continue;

					TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
					Entry->SetStringField(TEXT("name"), Asset.AssetName.ToString());
					Entry->SetStringField(TEXT("path"), Asset.GetObjectPathString());
					Entry->SetStringField(TEXT("class"), Asset.AssetClassPath.GetAssetName().ToString());
					Results.Add(MakeShared<FJsonValueObject>(Entry));
				}

				if (Results.Num() > 0)
				{
					Output->SetArrayField(CategoryName, Results);
				}
				TotalCount += Results.Num();
			};

			if (AssetType == TEXT("BehaviorTree") || AssetType == TEXT("All"))
			{
				CollectAssets(UBehaviorTree::StaticClass(), TEXT("behavior_trees"));
			}

			if (AssetType == TEXT("BlackboardData") || AssetType == TEXT("All"))
			{
				CollectAssets(UBlackboardData::StaticClass(), TEXT("blackboards"));
			}

			if (AssetType == TEXT("EnvQuery") || AssetType == TEXT("All"))
			{
				CollectAssets(UEnvQuery::StaticClass(), TEXT("eqs_queries"));
			}

			Output->SetNumberField(TEXT("total_count"), TotalCount);

			return FMCPToolResult::Success(JsonToString(Output));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// create_eqs_query - Create a new EnvQuery asset
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Content path for the new EQS query (e.g., '/Game/AI/EQS_FindCover')"), true);

		FMCPToolDefinition Def;
		Def.Name = TEXT("create_eqs_query");
		Def.Description = TEXT("Create a new Environment Query System (EQS) query asset at the specified content path.");
		Def.InputSchema = Schema;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));

			FString PackagePath = FPackageName::ObjectPathToPackageName(AssetPath);
			FString AssetName = FPackageName::GetShortName(AssetPath);

			UPackage* Package = CreatePackage(*PackagePath);
			if (!Package) return FMCPToolResult::Error(TEXT("Failed to create package"));

			UEnvQuery* NewEQS = NewObject<UEnvQuery>(Package, FName(*AssetName), RF_Public | RF_Standalone);
			if (!NewEQS) return FMCPToolResult::Error(TEXT("Failed to create EnvQuery object"));

			FAssetRegistryModule::AssetCreated(NewEQS);
			Package->MarkPackageDirty();

			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
			FString FilePath = FPackageName::LongPackageNameToFilename(PackagePath, FPackageName::GetAssetPackageExtension());
			UPackage::SavePackage(Package, NewEQS, *FilePath, SaveArgs);

			return FMCPToolResult::Success(FString::Printf(TEXT("Created EnvQuery asset: %s"), *AssetPath));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// set_bt_blackboard - Link a blackboard to a behavior tree
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("bt_path"), TEXT("Content path of the BehaviorTree asset"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("blackboard_path"), TEXT("Content path of the BlackboardData asset to link"), true);

		FMCPToolDefinition Def;
		Def.Name = TEXT("set_bt_blackboard");
		Def.Description = TEXT("Link a BlackboardData asset to a BehaviorTree. The behavior tree will use this blackboard for its AI context data.");
		Def.InputSchema = Schema;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString BTPath, BBPath;
			if (!Args->TryGetStringField(TEXT("bt_path"), BTPath))
				return FMCPToolResult::Error(TEXT("bt_path is required"));
			if (!Args->TryGetStringField(TEXT("blackboard_path"), BBPath))
				return FMCPToolResult::Error(TEXT("blackboard_path is required"));

			UBehaviorTree* BT = LoadObject<UBehaviorTree>(nullptr, *BTPath);
			if (!BT) return FMCPToolResult::Error(FString::Printf(TEXT("BehaviorTree not found: %s"), *BTPath));

			UBlackboardData* BB = LoadObject<UBlackboardData>(nullptr, *BBPath);
			if (!BB) return FMCPToolResult::Error(FString::Printf(TEXT("BlackboardData not found: %s"), *BBPath));

			BT->BlackboardAsset = BB;
			BT->MarkPackageDirty();

			return FMCPToolResult::Success(FString::Printf(TEXT("Linked blackboard '%s' to behavior tree '%s'"), *BB->GetName(), *BT->GetName()));
		});
		Registry.RegisterTool(Def);
	}
}

} // namespace MCPAITools
