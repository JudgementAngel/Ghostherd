// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "Tools/MCPDataTools.h"
#include "MCPToolBuilder.h"
#include "MCPValidate.h"
#include "MCPToolRegistry.h"
#include "MCPProtocol.h"

#include "Editor.h"
#include "Engine/DataTable.h"
#include "Engine/DataAsset.h"
#include "StructUtils/UserDefinedStruct.h" // moved from Engine/ in UE 5.5
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Kismet2/StructureEditorUtils.h"
#include "UserDefinedStructure/UserDefinedStructEditorData.h"
#include "Factories/StructureFactory.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "Common/MCPAssetCreate.h"

namespace MCPDataTools
{

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// list_datatables - List all DataTable assets in the project
	// ================================================================
	MCP_TOOL(Registry, "list_datatables")
		.Description(TEXT("List all DataTable assets in the project. Returns asset paths, row struct type, and row count."))
		.ReadOnly()
		.Idempotent()
		.StringArg(TEXT("path"), TEXT("Content path to search (default: '/Game/')"))
		.StringArg(TEXT("name_filter"), TEXT("Filter by name (substring match)"))
		.IntArg(TEXT("limit"), TEXT("Maximum results (default: 100)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));

			FString Path = TEXT("/Game/");
			Args->TryGetStringField(TEXT("path"), Path);

			FString NameFilter;
			Args->TryGetStringField(TEXT("name_filter"), NameFilter);

			int32 Limit = 100;
			if (Args->HasField(TEXT("limit")))
			{
				Limit = FMath::Clamp((int32)Args->GetNumberField(TEXT("limit")), 1, 1000);
			}

			FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
			IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

			TArray<FAssetData> Assets;
			AssetRegistry.GetAssetsByClass(UDataTable::StaticClass()->GetClassPathName(), Assets, true);

			TArray<TSharedPtr<FJsonValue>> Results;
			for (const FAssetData& Asset : Assets)
			{
				if (!Asset.PackagePath.ToString().StartsWith(Path))
					continue;

				if (!NameFilter.IsEmpty() && !Asset.AssetName.ToString().Contains(NameFilter))
					continue;

				if (Results.Num() >= Limit) break;

				TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("name"), Asset.AssetName.ToString());
				Entry->SetStringField(TEXT("path"), Asset.GetObjectPathString());

				// Try to get row struct info from tags
				FString RowStructPath;
				FAssetDataTagMapSharedView::FFindTagResult RowStructTag = Asset.TagsAndValues.FindTag(TEXT("RowStructure"));
				if (RowStructTag.IsSet())
				{
					Entry->SetStringField(TEXT("row_struct"), RowStructTag.GetValue());
				}

				// Load to get row count
				UDataTable* DT = Cast<UDataTable>(Asset.GetAsset());
				if (DT)
				{
					Entry->SetNumberField(TEXT("row_count"), DT->GetRowMap().Num());
				}

				Results.Add(MakeShared<FJsonValueObject>(Entry));
			}

			TSharedPtr<FJsonObject> Output = MakeShared<FJsonObject>();
			Output->SetNumberField(TEXT("count"), Results.Num());
			Output->SetArrayField(TEXT("datatables"), Results);

			return FMCPToolResult::SuccessStructured(JsonToString(Output), Output);
		});

	// ================================================================
	// get_datatable_rows - Read rows from a DataTable
	// ================================================================
	MCP_TOOL(Registry, "get_datatable_rows")
		.Description(TEXT("Read rows from a DataTable. Returns row names and all column values as JSON."))
		.ReadOnly()
		.Idempotent()
		.StringArg(TEXT("asset_path"), TEXT("Content path of the DataTable"), true)
		.StringArg(TEXT("row_filter"), TEXT("Filter rows by name (substring match)"))
		.IntArg(TEXT("limit"), TEXT("Maximum rows to return (default: 50)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));

			FString AssetPath;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("asset_path"), AssetPath));
			BAIL_IF_INVALID(FMCPValidate::AssetExists(AssetPath));

			UDataTable* DT = LoadObject<UDataTable>(nullptr, *AssetPath);
			if (!DT) return FMCPToolResult::Error(FString::Printf(TEXT("DataTable not found: %s"), *AssetPath));

			FString RowFilter;
			Args->TryGetStringField(TEXT("row_filter"), RowFilter);

			int32 Limit = 50;
			if (Args->HasField(TEXT("limit")))
			{
				Limit = FMath::Clamp((int32)Args->GetNumberField(TEXT("limit")), 1, 1000);
			}

			const UScriptStruct* RowStruct = DT->GetRowStruct();
			if (!RowStruct) return FMCPToolResult::Error(TEXT("DataTable has no row struct"));

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("datatable"), DT->GetName());
			Result->SetStringField(TEXT("row_struct"), RowStruct->GetName());
			Result->SetNumberField(TEXT("total_rows"), DT->GetRowMap().Num());

			// Column names
			TArray<TSharedPtr<FJsonValue>> Columns;
			for (TFieldIterator<FProperty> PropIt(RowStruct); PropIt; ++PropIt)
			{
				TSharedPtr<FJsonObject> Col = MakeShared<FJsonObject>();
				Col->SetStringField(TEXT("name"), PropIt->GetName());
				Col->SetStringField(TEXT("type"), PropIt->GetCPPType());
				Columns.Add(MakeShared<FJsonValueObject>(Col));
			}
			Result->SetArrayField(TEXT("columns"), Columns);

			// Rows
			TArray<TSharedPtr<FJsonValue>> Rows;
			const TMap<FName, uint8*>& RowMap = DT->GetRowMap();

			for (const auto& Pair : RowMap)
			{
				if (!RowFilter.IsEmpty() && !Pair.Key.ToString().Contains(RowFilter))
					continue;

				if (Rows.Num() >= Limit) break;

				TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
				Row->SetStringField(TEXT("row_name"), Pair.Key.ToString());

				TSharedPtr<FJsonObject> Values = MakeShared<FJsonObject>();
				for (TFieldIterator<FProperty> PropIt(RowStruct); PropIt; ++PropIt)
				{
					FProperty* Prop = *PropIt;
					FString ValueStr;
					const void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Pair.Value);
					Prop->ExportTextItem_Direct(ValueStr, ValuePtr, nullptr, nullptr, PPF_None);
					Values->SetStringField(Prop->GetName(), ValueStr);
				}
				Row->SetObjectField(TEXT("values"), Values);
				Rows.Add(MakeShared<FJsonValueObject>(Row));
			}

			Result->SetNumberField(TEXT("returned_rows"), Rows.Num());
			Result->SetArrayField(TEXT("rows"), Rows);

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});

	// ================================================================
	// add_datatable_row - Add a row to a DataTable
	// ================================================================
	MCP_TOOL(Registry, "add_datatable_row")
		.Description(TEXT("Add a new row to a DataTable. Provide column values as a JSON string with property names matching the row struct."))
		.StringArg(TEXT("asset_path"), TEXT("Content path of the DataTable"), true)
		.StringArg(TEXT("row_name"), TEXT("Name for the new row"), true)
		.StringArg(TEXT("row_json"), TEXT("JSON object with column name/value pairs"), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));

			FString AssetPath, RowName, RowJson;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("asset_path"), AssetPath));
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("row_name"), RowName));
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("row_json"), RowJson));
			BAIL_IF_INVALID(FMCPValidate::AssetExists(AssetPath));
			BAIL_IF_INVALID(FMCPValidate::AssetName(RowName));

			UDataTable* DT = LoadObject<UDataTable>(nullptr, *AssetPath);
			if (!DT) return FMCPToolResult::Error(FString::Printf(TEXT("DataTable not found: %s"), *AssetPath));

			const UScriptStruct* RowStruct = DT->GetRowStruct();
			if (!RowStruct) return FMCPToolResult::Error(TEXT("DataTable has no row struct"));

			// Check if row already exists
			if (DT->GetRowMap().Contains(FName(*RowName)))
			{
				return FMCPToolResult::Error(FString::Printf(TEXT("Row '%s' already exists"), *RowName));
			}

			// Parse JSON values
			TSharedPtr<FJsonObject> ValuesJson = StringToJson(RowJson);
			if (!ValuesJson.IsValid()) return FMCPToolResult::Error(TEXT("Failed to parse row_json as JSON"));

			// Allocate row data
			uint8* RowData = (uint8*)FMemory::Malloc(RowStruct->GetStructureSize());
			RowStruct->InitializeStruct(RowData);

			// Set values from JSON
			int32 SetCount = 0;
			for (TFieldIterator<FProperty> PropIt(RowStruct); PropIt; ++PropIt)
			{
				FProperty* Prop = *PropIt;
				FString PropValue;
				if (ValuesJson->TryGetStringField(Prop->GetName(), PropValue))
				{
					void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(RowData);
					if (Prop->ImportText_Direct(*PropValue, ValuePtr, nullptr, PPF_None))
					{
						SetCount++;
					}
				}
			}

			// Add to table
			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Add DataTable Row")));
			DT->Modify();
			DT->AddRow(FName(*RowName), *reinterpret_cast<FTableRowBase*>(RowData));
			DT->MarkPackageDirty();
			GEditor->EndTransaction();

			RowStruct->DestroyStruct(RowData);
			FMemory::Free(RowData);

			return FMCPToolResult::Success(FString::Printf(TEXT("Added row '%s' to '%s' (%d values set)"),
				*RowName, *DT->GetName(), SetCount));
		});

	// ================================================================
	// create_user_struct - Create a new UserDefinedStruct
	// ================================================================
	MCP_TOOL(Registry, "create_user_struct")
		.Description(TEXT("Create a new UserDefinedStruct (Data Structure) asset. Optionally pre-populate with typed fields. The struct can be used in Blueprints, DataTables, and variables. Supported field types: Boolean, Integer, Float, String, Name, Text, Vector, Rotator, Transform, LinearColor, Object, SoftObject."))
		.StringArg(TEXT("asset_path"), TEXT("Content path for the new struct (e.g., '/Game/Data/S_WeaponData')"), true)
		.StringArg(TEXT("fields_json"), TEXT("Optional JSON array of field definitions: [{\"name\":\"Damage\",\"type\":\"Float\"},{\"name\":\"Weapon\",\"type\":\"Object\",\"class\":\"/Script/Engine.Actor\"}]. Supported types: Boolean, Integer, Float, String, Name, Text, Vector, Rotator, Transform, LinearColor, Object, SoftObject. For Object/SoftObject, optional 'class' specifies the class path (default: /Script/CoreUObject.Object)."))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));

			FString AssetPath;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("asset_path"), AssetPath));

			// Extract package path and asset name
			FString PackagePath, AssetName;
			int32 LastSlash;
			if (!AssetPath.FindLastChar('/', LastSlash))
				return FMCPToolResult::Error(TEXT("Invalid asset_path format. Use '/Game/Path/StructName'."));

			PackagePath = AssetPath.Left(LastSlash);
			AssetName = AssetPath.RightChop(LastSlash + 1);

			BAIL_IF_INVALID(FMCPValidate::PackagePath(PackagePath));
			BAIL_IF_INVALID(FMCPValidate::AssetName(AssetName));
			BAIL_IF_INVALID(FMCPValidate::AssetDoesNotExist(PackagePath / AssetName));

			// Create the struct via AssetTools. UserDefinedStructs MUST go through
			// UStructureFactory (FStructureEditorUtils::CreateUserDefinedStruct): a
			// plain NewObject leaves EditorData null, and the first
			// FStructureEditorUtils call then hits a fatal CastChecked and crashes
			// the editor.
			IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
			UStructureFactory* StructFactory = NewObject<UStructureFactory>();

			UUserDefinedStruct* NewStruct = Cast<UUserDefinedStruct>(
				AssetTools.CreateAsset(AssetName, PackagePath, UUserDefinedStruct::StaticClass(), StructFactory));

			if (!NewStruct)
				return FMCPToolResult::Error(FString::Printf(TEXT("Failed to create UserDefinedStruct at: %s"), *AssetPath));

			// Journal the creation so run_tool_script can report truthfully that this
			// asset survives a rollback (UE package creation is not transactional).
			MCPCommon::NoteAssetCreated(PackagePath / AssetName);

			// The factory seeds the struct with one default Boolean member
			// (structs must always have at least one variable). Remember it so it
			// can be dropped once real fields have been added.
			FGuid DefaultVarGuid;
			{
				const TArray<FStructVariableDescription>& VarDesc = FStructureEditorUtils::GetVarDesc(NewStruct);
				if (VarDesc.Num() > 0)
				{
					DefaultVarGuid = VarDesc[0].VarGuid;
				}
			}

			// Add fields if provided
			FString FieldsJsonStr;
			int32 FieldCount = 0;
			if (Args->TryGetStringField(TEXT("fields_json"), FieldsJsonStr))
			{
				TArray<TSharedPtr<FJsonValue>> FieldsArray;
				TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(FieldsJsonStr);
				if (FJsonSerializer::Deserialize(Reader, FieldsArray))
				{
					for (const auto& FieldValue : FieldsArray)
					{
						const TSharedPtr<FJsonObject>* FieldObj;
						if (!FieldValue->TryGetObject(FieldObj)) continue;

						FString FieldName, FieldType;
						(*FieldObj)->TryGetStringField(TEXT("name"), FieldName);
						(*FieldObj)->TryGetStringField(TEXT("type"), FieldType);
						if (FieldName.IsEmpty() || FieldType.IsEmpty()) continue;

						// Map type string to FEdGraphPinType
						FEdGraphPinType PinType;
						PinType.PinCategory = NAME_None;

						if (FieldType == TEXT("Boolean") || FieldType == TEXT("bool"))
							PinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
						else if (FieldType == TEXT("Integer") || FieldType == TEXT("Int"))
							PinType.PinCategory = UEdGraphSchema_K2::PC_Int;
						else if (FieldType == TEXT("Float") || FieldType == TEXT("Double"))
						{
							// PC_Real requires a float/double subcategory to be a valid pin type
							PinType.PinCategory = UEdGraphSchema_K2::PC_Real;
							PinType.PinSubCategory = UEdGraphSchema_K2::PC_Double;
						}
						else if (FieldType == TEXT("String"))
							PinType.PinCategory = UEdGraphSchema_K2::PC_String;
						else if (FieldType == TEXT("Name"))
							PinType.PinCategory = UEdGraphSchema_K2::PC_Name;
						else if (FieldType == TEXT("Text"))
							PinType.PinCategory = UEdGraphSchema_K2::PC_Text;
						else if (FieldType == TEXT("Vector"))
						{
							PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
							PinType.PinSubCategoryObject = TBaseStructure<FVector>::Get();
						}
						else if (FieldType == TEXT("Rotator"))
						{
							PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
							PinType.PinSubCategoryObject = TBaseStructure<FRotator>::Get();
						}
						else if (FieldType == TEXT("Transform"))
						{
							PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
							PinType.PinSubCategoryObject = TBaseStructure<FTransform>::Get();
						}
						else if (FieldType == TEXT("LinearColor"))
						{
							PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
							PinType.PinSubCategoryObject = TBaseStructure<FLinearColor>::Get();
						}
						else if (FieldType == TEXT("Object") || FieldType == TEXT("SoftObject"))
						{
							PinType.PinCategory = (FieldType == TEXT("Object"))
								? UEdGraphSchema_K2::PC_Object
								: UEdGraphSchema_K2::PC_SoftObject;

							UClass* FieldClass = UObject::StaticClass();
							FString ClassPath;
							if ((*FieldObj)->TryGetStringField(TEXT("class"), ClassPath) && !ClassPath.IsEmpty())
							{
								if (UClass* Loaded = LoadObject<UClass>(nullptr, *ClassPath))
								{
									FieldClass = Loaded;
								}
							}
							PinType.PinSubCategoryObject = FieldClass;
						}
						else
						{
							continue; // Skip unknown types
						}

						if (PinType.PinCategory != NAME_None)
						{
							FStructureEditorUtils::AddVariable(NewStruct, PinType);
							// The last added variable gets a default name — rename it
							TArray<FStructVariableDescription>& VarDesc = FStructureEditorUtils::GetVarDesc(NewStruct);
							if (VarDesc.Num() > 0)
							{
								FGuid VarGuid = VarDesc.Last().VarGuid;
								FStructureEditorUtils::RenameVariable(NewStruct, VarGuid, FieldName);
								FieldCount++;
							}
						}
					}
				}
			}

			// Once real fields exist, remove the factory's seed Boolean member so
			// the struct contains exactly the requested fields. (Skipped when no
			// fields were given — a struct must always keep at least one variable.)
			if (FieldCount > 0 && DefaultVarGuid.IsValid())
			{
				FStructureEditorUtils::RemoveVariable(NewStruct, DefaultVarGuid);
			}

			NewStruct->MarkPackageDirty();

			// Persist to disk so the asset survives an editor crash.
			// (CreateAsset has already notified the asset registry.)
			if (UPackage* Package = NewStruct->GetPackage())
			{
				const FString PackageFilename = FPackageName::LongPackageNameToFilename(
					Package->GetName(), FPackageName::GetAssetPackageExtension());
				FSavePackageArgs SaveArgs;
				SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
				SaveArgs.SaveFlags = SAVE_NoError;
				UPackage::SavePackage(Package, NewStruct, *PackageFilename, SaveArgs);
			}

			// Report result
			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("asset_path"), NewStruct->GetPathName());
			Result->SetStringField(TEXT("struct_name"), NewStruct->GetName());
			Result->SetNumberField(TEXT("field_count"), FieldCount);

			// List actual fields
			TArray<TSharedPtr<FJsonValue>> FieldsList;
			for (TFieldIterator<FProperty> It(NewStruct); It; ++It)
			{
				TSharedPtr<FJsonObject> FObj = MakeShared<FJsonObject>();
				FObj->SetStringField(TEXT("name"), It->GetName());
				FObj->SetStringField(TEXT("type"), It->GetCPPType());
				FieldsList.Add(MakeShared<FJsonValueObject>(FObj));
			}
			Result->SetArrayField(TEXT("fields"), FieldsList);

			FString JsonStr;
			auto Writer = TJsonWriterFactory<>::Create(&JsonStr);
			FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
			return FMCPToolResult::Success(JsonStr);
		});

	// ================================================================
	// get_struct_info - Inspect a UserDefinedStruct's fields
	// ================================================================
	MCP_TOOL(Registry, "get_struct_info")
		.Description(TEXT("Get detailed info about a UserDefinedStruct or any UScriptStruct: field names, types, and default values. Works for both Blueprint-created structs and engine structs."))
		.ReadOnly()
		.Idempotent()
		.StringArg(TEXT("asset_path"), TEXT("Content path of the struct asset"), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));

			FString AssetPath;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("asset_path"), AssetPath));

			UScriptStruct* Struct = LoadObject<UScriptStruct>(nullptr, *AssetPath);
			if (!Struct)
			{
				// Try as UserDefinedStruct
				Struct = LoadObject<UUserDefinedStruct>(nullptr, *AssetPath);
			}
			if (!Struct)
				return FMCPToolResult::Error(FString::Printf(TEXT("Struct not found: %s"), *AssetPath));

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("struct_name"), Struct->GetName());
			Result->SetStringField(TEXT("asset_path"), Struct->GetPathName());
			Result->SetNumberField(TEXT("size_bytes"), Struct->GetStructureSize());

			UUserDefinedStruct* UDS = Cast<UUserDefinedStruct>(Struct);
			Result->SetBoolField(TEXT("is_user_defined"), UDS != nullptr);

			TArray<TSharedPtr<FJsonValue>> FieldsList;
			for (TFieldIterator<FProperty> It(Struct); It; ++It)
			{
				FProperty* Prop = *It;
				TSharedPtr<FJsonObject> FObj = MakeShared<FJsonObject>();
				FObj->SetStringField(TEXT("name"), Prop->GetName());
				FObj->SetStringField(TEXT("type"), Prop->GetCPPType());
				FObj->SetNumberField(TEXT("offset"), Prop->GetOffset_ForInternal());

				// Get default value as string
				if (UDS)
				{
					uint8* DefaultData = (uint8*)FMemory::Malloc(Struct->GetStructureSize());
					Struct->InitializeStruct(DefaultData);
					UDS->InitializeDefaultValue(DefaultData);

					FString DefaultStr;
					void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(DefaultData);
					Prop->ExportTextItem_Direct(DefaultStr, ValuePtr, nullptr, nullptr, PPF_None);
					if (!DefaultStr.IsEmpty())
						FObj->SetStringField(TEXT("default_value"), DefaultStr);

					Struct->DestroyStruct(DefaultData);
					FMemory::Free(DefaultData);
				}

				FieldsList.Add(MakeShared<FJsonValueObject>(FObj));
			}
			Result->SetArrayField(TEXT("fields"), FieldsList);

			FString JsonStr;
			auto Writer = TJsonWriterFactory<>::Create(&JsonStr);
			FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
			return FMCPToolResult::Success(JsonStr);
		});

	// ================================================================
	// list_user_structs - List all UserDefinedStruct assets
	// ================================================================
	MCP_TOOL(Registry, "list_user_structs")
		.Description(TEXT("List all UserDefinedStruct assets in the project. Returns name, path, and field count for each struct."))
		.ReadOnly()
		.Idempotent()
		.StringArg(TEXT("path"), TEXT("Content path to search (default: '/Game/')"))
		.StringArg(TEXT("name_filter"), TEXT("Filter by name (substring match)"))
		.IntArg(TEXT("limit"), TEXT("Maximum results (default: 100)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));

			FString SearchPath = TEXT("/Game/");
			Args->TryGetStringField(TEXT("path"), SearchPath);

			FString NameFilter;
			Args->TryGetStringField(TEXT("name_filter"), NameFilter);

			int32 Limit = 100;
			double LimitD;
			if (Args->TryGetNumberField(TEXT("limit"), LimitD)) Limit = (int32)LimitD;

			IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();

			FARFilter Filter;
			Filter.ClassPaths.Add(UUserDefinedStruct::StaticClass()->GetClassPathName());
			Filter.PackagePaths.Add(FName(*SearchPath));
			Filter.bRecursivePaths = true;

			TArray<FAssetData> Assets;
			AR.GetAssets(Filter, Assets);

			TArray<TSharedPtr<FJsonValue>> Results;
			for (const FAssetData& Asset : Assets)
			{
				if (Results.Num() >= Limit) break;

				FString Name = Asset.AssetName.ToString();
				if (!NameFilter.IsEmpty() && !Name.Contains(NameFilter)) continue;

				TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
				Obj->SetStringField(TEXT("name"), Name);
				Obj->SetStringField(TEXT("path"), Asset.GetObjectPathString());

				// Try to count fields
				if (UUserDefinedStruct* Struct = Cast<UUserDefinedStruct>(Asset.GetAsset()))
				{
					int32 Count = 0;
					for (TFieldIterator<FProperty> It(Struct); It; ++It) Count++;
					Obj->SetNumberField(TEXT("field_count"), Count);
				}

				Results.Add(MakeShared<FJsonValueObject>(Obj));
			}

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetNumberField(TEXT("count"), Results.Num());
			Result->SetArrayField(TEXT("structs"), Results);

			FString JsonStr;
			auto Writer = TJsonWriterFactory<>::Create(&JsonStr);
			FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
			return FMCPToolResult::Success(JsonStr);
		});
}

} // namespace MCPDataTools
