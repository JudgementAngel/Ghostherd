#include "Tools/MCPBatchTools.h"
#include "MCPToolRegistry.h"
#include "MCPProtocol.h"

#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"

namespace MCPBatchTools
{

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

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// batch_transform - Move/rotate/scale multiple actors at once
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddStringArray(Schema, TEXT("actor_names"), TEXT("Array of actor labels to transform"), true);
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("x"), TEXT("X position"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("y"), TEXT("Y position"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("z"), TEXT("Z position"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("pitch"), TEXT("Pitch rotation in degrees"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("yaw"), TEXT("Yaw rotation in degrees"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("roll"), TEXT("Roll rotation in degrees"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("scale_x"), TEXT("X scale"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("scale_y"), TEXT("Y scale"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("scale_z"), TEXT("Z scale"));
		FMCPSchemaBuilder::AddBoolean(Schema, TEXT("relative"), TEXT("If true, values are added to current transform. If false, values are set absolutely (default: false)."));

		FMCPToolDefinition Def;
		Def.Name = TEXT("batch_transform");
		Def.Description = TEXT("Apply the same transform change to multiple actors at once. Only provided fields are changed; omitted fields keep their current values.");
		Def.InputSchema = Schema;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			TArray<TSharedPtr<FJsonValue>> Names = Args->GetArrayField(TEXT("actor_names"));
			if (Names.Num() == 0) return FMCPToolResult::Error(TEXT("No actor names provided"));

			bool bRelative = false;
			Args->TryGetBoolField(TEXT("relative"), bRelative);

			// Collect target names
			TSet<FString> TargetNames;
			for (const auto& Val : Names)
			{
				FString Name;
				if (Val->TryGetString(Name)) TargetNames.Add(Name);
			}

			// Find matching actors
			TArray<AActor*> Actors;
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				if (TargetNames.Contains((*It)->GetActorLabel()))
				{
					Actors.Add(*It);
				}
			}

			if (Actors.Num() == 0) return FMCPToolResult::Error(TEXT("No matching actors found"));

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Batch Transform")));

			int32 Transformed = 0;
			for (AActor* Actor : Actors)
			{
				Actor->Modify();

				FVector Loc = Actor->GetActorLocation();
				FRotator Rot = Actor->GetActorRotation();
				FVector Scale = Actor->GetActorScale3D();

				if (bRelative)
				{
					if (Args->HasField(TEXT("x"))) Loc.X += Args->GetNumberField(TEXT("x"));
					if (Args->HasField(TEXT("y"))) Loc.Y += Args->GetNumberField(TEXT("y"));
					if (Args->HasField(TEXT("z"))) Loc.Z += Args->GetNumberField(TEXT("z"));
					if (Args->HasField(TEXT("pitch"))) Rot.Pitch += Args->GetNumberField(TEXT("pitch"));
					if (Args->HasField(TEXT("yaw"))) Rot.Yaw += Args->GetNumberField(TEXT("yaw"));
					if (Args->HasField(TEXT("roll"))) Rot.Roll += Args->GetNumberField(TEXT("roll"));
					if (Args->HasField(TEXT("scale_x"))) Scale.X += Args->GetNumberField(TEXT("scale_x"));
					if (Args->HasField(TEXT("scale_y"))) Scale.Y += Args->GetNumberField(TEXT("scale_y"));
					if (Args->HasField(TEXT("scale_z"))) Scale.Z += Args->GetNumberField(TEXT("scale_z"));
				}
				else
				{
					if (Args->HasField(TEXT("x"))) Loc.X = Args->GetNumberField(TEXT("x"));
					if (Args->HasField(TEXT("y"))) Loc.Y = Args->GetNumberField(TEXT("y"));
					if (Args->HasField(TEXT("z"))) Loc.Z = Args->GetNumberField(TEXT("z"));
					if (Args->HasField(TEXT("pitch"))) Rot.Pitch = Args->GetNumberField(TEXT("pitch"));
					if (Args->HasField(TEXT("yaw"))) Rot.Yaw = Args->GetNumberField(TEXT("yaw"));
					if (Args->HasField(TEXT("roll"))) Rot.Roll = Args->GetNumberField(TEXT("roll"));
					if (Args->HasField(TEXT("scale_x"))) Scale.X = Args->GetNumberField(TEXT("scale_x"));
					if (Args->HasField(TEXT("scale_y"))) Scale.Y = Args->GetNumberField(TEXT("scale_y"));
					if (Args->HasField(TEXT("scale_z"))) Scale.Z = Args->GetNumberField(TEXT("scale_z"));
				}

				Actor->SetActorLocation(Loc);
				Actor->SetActorRotation(Rot);
				Actor->SetActorScale3D(Scale);
				Transformed++;
			}

			GEditor->EndTransaction();

			return FMCPToolResult::Success(FString::Printf(TEXT("Transformed %d of %d requested actors"), Transformed, TargetNames.Num()));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// batch_set_property - Set same property on multiple actors
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddStringArray(Schema, TEXT("actor_names"), TEXT("Array of actor labels"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("property_name"), TEXT("Name of the UPROPERTY to set"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("property_value"), TEXT("New value as a string (parsed by UE property system)"), true);

		FMCPToolDefinition Def;
		Def.Name = TEXT("batch_set_property");
		Def.Description = TEXT("Set the same property value on multiple actors at once. Uses UE's property system for value parsing.");
		Def.InputSchema = Schema;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			FString PropName, PropValue;
			if (!Args->TryGetStringField(TEXT("property_name"), PropName)) return FMCPToolResult::Error(TEXT("property_name required"));
			if (!Args->TryGetStringField(TEXT("property_value"), PropValue)) return FMCPToolResult::Error(TEXT("property_value required"));

			TArray<TSharedPtr<FJsonValue>> Names = Args->GetArrayField(TEXT("actor_names"));
			if (Names.Num() == 0) return FMCPToolResult::Error(TEXT("No actor names provided"));

			TSet<FString> TargetNames;
			for (const auto& Val : Names)
			{
				FString Name;
				if (Val->TryGetString(Name)) TargetNames.Add(Name);
			}

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Batch Set Property")));

			int32 Updated = 0;
			TArray<FString> Errors;

			for (TActorIterator<AActor> It(World); It; ++It)
			{
				AActor* Actor = *It;
				if (!Actor || !TargetNames.Contains(Actor->GetActorLabel()))
					continue;

				FProperty* Prop = Actor->GetClass()->FindPropertyByName(FName(*PropName));
				if (!Prop)
				{
					Errors.Add(FString::Printf(TEXT("'%s': property '%s' not found"), *Actor->GetActorLabel(), *PropName));
					continue;
				}

				Actor->Modify();
				void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Actor);
				if (Prop->ImportText_Direct(*PropValue, ValuePtr, Actor, PPF_None))
				{
					Actor->PostEditChange();
					Updated++;
				}
				else
				{
					Errors.Add(FString::Printf(TEXT("'%s': failed to parse value"), *Actor->GetActorLabel()));
				}
			}

			GEditor->EndTransaction();

			FString Result = FString::Printf(TEXT("Set '%s' = '%s' on %d of %d actors"), *PropName, *PropValue, Updated, TargetNames.Num());
			if (Errors.Num() > 0)
			{
				Result += TEXT("\nErrors: ") + FString::Join(Errors, TEXT("; "));
			}
			return FMCPToolResult::Success(Result);
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// find_actors - Advanced actor query with combined filters
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("class_filter"), TEXT("Filter by class name (substring match)"));
		FMCPSchemaBuilder::AddString(Schema, TEXT("name_pattern"), TEXT("Filter by actor label (substring match, case-insensitive)"));
		FMCPSchemaBuilder::AddString(Schema, TEXT("tag"), TEXT("Filter by actor tag (exact match)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("near_x"), TEXT("Center X for proximity search"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("near_y"), TEXT("Center Y for proximity search"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("near_z"), TEXT("Center Z for proximity search"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("radius"), TEXT("Search radius around near_x/y/z (in cm)"));
		FMCPSchemaBuilder::AddBoolean(Schema, TEXT("hidden_only"), TEXT("Only return hidden actors"));
		FMCPSchemaBuilder::AddBoolean(Schema, TEXT("visible_only"), TEXT("Only return visible actors"));
		FMCPSchemaBuilder::AddInteger(Schema, TEXT("limit"), TEXT("Maximum results (default: 100)"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("find_actors");
		Def.Description = TEXT("Advanced actor query combining class, name, tag, and proximity filters. More powerful than list_actors for targeted searches.");
		Def.InputSchema = Schema;
		Def.bReadOnlyHint = true;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			FString ClassFilter, NamePattern, TagFilter;
			Args->TryGetStringField(TEXT("class_filter"), ClassFilter);
			Args->TryGetStringField(TEXT("name_pattern"), NamePattern);
			Args->TryGetStringField(TEXT("tag"), TagFilter);

			bool bHasProximity = Args->HasField(TEXT("near_x")) || Args->HasField(TEXT("near_y")) || Args->HasField(TEXT("near_z"));
			FVector SearchCenter(
				Args->HasField(TEXT("near_x")) ? Args->GetNumberField(TEXT("near_x")) : 0.0,
				Args->HasField(TEXT("near_y")) ? Args->GetNumberField(TEXT("near_y")) : 0.0,
				Args->HasField(TEXT("near_z")) ? Args->GetNumberField(TEXT("near_z")) : 0.0
			);
			double SearchRadius = Args->HasField(TEXT("radius")) ? Args->GetNumberField(TEXT("radius")) : 0.0;
			if (bHasProximity && SearchRadius <= 0.0) SearchRadius = 1000.0; // Default 10m radius

			bool bHiddenOnly = false, bVisibleOnly = false;
			Args->TryGetBoolField(TEXT("hidden_only"), bHiddenOnly);
			Args->TryGetBoolField(TEXT("visible_only"), bVisibleOnly);

			int32 Limit = 100;
			if (Args->HasField(TEXT("limit")))
			{
				Limit = FMath::Clamp((int32)Args->GetNumberField(TEXT("limit")), 1, 5000);
			}

			TArray<TSharedPtr<FJsonValue>> Results;
			int32 TotalMatches = 0;

			for (TActorIterator<AActor> It(World); It; ++It)
			{
				AActor* Actor = *It;
				if (!IsValid(Actor)) continue;

				// Class filter
				if (!ClassFilter.IsEmpty() && !Actor->GetClass()->GetName().Contains(ClassFilter))
					continue;

				// Name pattern
				if (!NamePattern.IsEmpty() && !Actor->GetActorLabel().Contains(NamePattern))
					continue;

				// Tag filter
				if (!TagFilter.IsEmpty())
				{
					if (!Actor->Tags.Contains(FName(*TagFilter)))
						continue;
				}

				// Proximity filter
				if (bHasProximity)
				{
					double Dist = FVector::Dist(Actor->GetActorLocation(), SearchCenter);
					if (Dist > SearchRadius) continue;
				}

				// Visibility filter
				if (bHiddenOnly && !Actor->IsHidden()) continue;
				if (bVisibleOnly && Actor->IsHidden()) continue;

				TotalMatches++;
				if (Results.Num() < Limit)
				{
					TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
					Entry->SetStringField(TEXT("name"), Actor->GetActorLabel());
					Entry->SetStringField(TEXT("class"), Actor->GetClass()->GetName());

					FVector Loc = Actor->GetActorLocation();
					Entry->SetNumberField(TEXT("x"), Loc.X);
					Entry->SetNumberField(TEXT("y"), Loc.Y);
					Entry->SetNumberField(TEXT("z"), Loc.Z);
					Entry->SetBoolField(TEXT("hidden"), Actor->IsHidden());
					Entry->SetStringField(TEXT("folder"), Actor->GetFolderPath().ToString());

					if (bHasProximity)
					{
						Entry->SetNumberField(TEXT("distance"), FVector::Dist(Actor->GetActorLocation(), SearchCenter));
					}

					// Tags
					TArray<TSharedPtr<FJsonValue>> TagsArr;
					for (const FName& Tag : Actor->Tags)
					{
						TagsArr.Add(MakeShared<FJsonValueString>(Tag.ToString()));
					}
					Entry->SetArrayField(TEXT("tags"), TagsArr);

					Results.Add(MakeShared<FJsonValueObject>(Entry));
				}
			}

			TSharedPtr<FJsonObject> Output = MakeShared<FJsonObject>();
			Output->SetNumberField(TEXT("total_matches"), TotalMatches);
			Output->SetNumberField(TEXT("returned"), Results.Num());
			Output->SetArrayField(TEXT("actors"), Results);

			return FMCPToolResult::Success(JsonToString(Output));
		});
		Registry.RegisterTool(Def);
	}
}

} // namespace MCPBatchTools
