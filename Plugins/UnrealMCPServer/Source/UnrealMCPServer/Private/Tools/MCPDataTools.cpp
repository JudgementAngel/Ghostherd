#include "Tools/MCPDataTools.h"
#include "MCPToolRegistry.h"
#include "MCPProtocol.h"

#include "Editor.h"
#include "Engine/DataTable.h"
#include "Engine/DataAsset.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"

namespace MCPDataTools
{

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// list_datatables - List all DataTable assets in the project
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("path"), TEXT("Content path to search (default: '/Game/')"));
		FMCPSchemaBuilder::AddString(Schema, TEXT("name_filter"), TEXT("Filter by name (substring match)"));
		FMCPSchemaBuilder::AddInteger(Schema, TEXT("limit"), TEXT("Maximum results (default: 100)"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("list_datatables");
		Def.Description = TEXT("List all DataTable assets in the project. Returns asset paths, row struct type, and row count.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
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

			return FMCPToolResult::Success(JsonToString(Output));
		});
		Def.bReadOnlyHint = true;
		Def.bIdempotentHint = true;
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// get_datatable_rows - Read rows from a DataTable
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Content path of the DataTable"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("row_filter"), TEXT("Filter rows by name (substring match)"));
		FMCPSchemaBuilder::AddInteger(Schema, TEXT("limit"), TEXT("Maximum rows to return (default: 50)"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("get_datatable_rows");
		Def.Description = TEXT("Read rows from a DataTable. Returns row names and all column values as JSON.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath)) return FMCPToolResult::Error(TEXT("asset_path required"));

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

			return FMCPToolResult::Success(JsonToString(Result));
		});
		Def.bReadOnlyHint = true;
		Def.bIdempotentHint = true;
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// add_datatable_row - Add a row to a DataTable
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Content path of the DataTable"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("row_name"), TEXT("Name for the new row"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("row_json"), TEXT("JSON object with column name/value pairs"), true);

		FMCPToolDefinition Def;
		Def.Name = TEXT("add_datatable_row");
		Def.Description = TEXT("Add a new row to a DataTable. Provide column values as a JSON string with property names matching the row struct.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath, RowName, RowJson;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath)) return FMCPToolResult::Error(TEXT("asset_path required"));
			if (!Args->TryGetStringField(TEXT("row_name"), RowName)) return FMCPToolResult::Error(TEXT("row_name required"));
			if (!Args->TryGetStringField(TEXT("row_json"), RowJson)) return FMCPToolResult::Error(TEXT("row_json required"));

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
		Registry.RegisterTool(Def);
	}
}

} // namespace MCPDataTools
