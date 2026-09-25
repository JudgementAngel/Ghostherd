// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "Tools/MCPBatchTools.h"
#include "Common/MCPActorResolver.h"
#include "MCPToolRegistry.h"
#include "MCPProtocol.h"
#include "MCPToolBuilder.h"

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
	// v4 Phase 1: cached resolver (O(1) amortized) replaces the per-call actor scan.
	return MCPCommon::FindActorByLabel(World, Label);
}

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// batch_transform - Move/rotate/scale multiple actors at once
	// ================================================================
	MCP_TOOL(Registry, "batch_transform")
		.Description(TEXT("Apply the same transform change to multiple actors at once. Only provided fields are changed; omitted fields keep their current values."))
		.Idempotent()
		.StringArrayArg(TEXT("actor_names"), TEXT("Array of actor labels to transform"), true)
		.NumberArg(TEXT("x"), TEXT("X position"))
		.NumberArg(TEXT("y"), TEXT("Y position"))
		.NumberArg(TEXT("z"), TEXT("Z position"))
		.NumberArg(TEXT("pitch"), TEXT("Pitch rotation in degrees"))
		.NumberArg(TEXT("yaw"), TEXT("Yaw rotation in degrees"))
		.NumberArg(TEXT("roll"), TEXT("Roll rotation in degrees"))
		.NumberArg(TEXT("scale_x"), TEXT("X scale"))
		.NumberArg(TEXT("scale_y"), TEXT("Y scale"))
		.NumberArg(TEXT("scale_z"), TEXT("Z scale"))
		.BoolArg(TEXT("relative"), TEXT("If true, values are added to current transform. If false, values are set absolutely (default: false)."))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			// v4 Phase 3: TryGet avoids LogJson warnings on missing field;
			// the existing empty-checks below handle the error path.
			TArray<TSharedPtr<FJsonValue>> Names;
			if (const TArray<TSharedPtr<FJsonValue>>* NamesPtr = nullptr;
				Args->TryGetArrayField(TEXT("actor_names"), NamesPtr) && NamesPtr)
			{
				Names = *NamesPtr;
			}
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

			// v4 Phase 0 (bug fix): resolve every requested actor BEFORE opening the
			// transaction. v3 transformed whatever subset existed and committed it,
			// leaving agents with silent partial application they couldn't detect.
			// Batch mutations are now all-or-nothing.
			TMap<FString, AActor*> Resolved;
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				if (TargetNames.Contains((*It)->GetActorLabel()))
				{
					Resolved.Add((*It)->GetActorLabel(), *It);
				}
			}

			TArray<FString> Missing;
			for (const FString& Name : TargetNames)
			{
				if (!Resolved.Contains(Name)) Missing.Add(Name);
			}
			if (Missing.Num() > 0)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
					FString::Printf(TEXT("%d of %d actors not found: %s. Nothing was modified."),
						Missing.Num(), TargetNames.Num(), *FString::Join(Missing, TEXT(", "))),
					TEXT("Batch tools are all-or-nothing. Fix the labels (find_actors helps) and retry."));
			}

			TArray<AActor*> Actors;
			Resolved.GenerateValueArray(Actors);

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

			return FMCPToolResult::Success(FString::Printf(TEXT("Transformed all %d requested actors"), Transformed));
		});

	// ================================================================
	// batch_set_property - Set same property on multiple actors
	// ================================================================
	MCP_TOOL(Registry, "batch_set_property")
		.Description(TEXT("Set the same property value on multiple actors at once. Uses UE's property system for value parsing."))
		.Idempotent()
		.StringArrayArg(TEXT("actor_names"), TEXT("Array of actor labels"), true)
		.StringArg(TEXT("property_name"), TEXT("Name of the UPROPERTY to set"), true)
		.StringArg(TEXT("property_value"), TEXT("New value as a string (parsed by UE property system)"), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			FString PropName, PropValue;
			if (!Args->TryGetStringField(TEXT("property_name"), PropName)) return FMCPToolResult::Error(TEXT("property_name required"));
			if (!Args->TryGetStringField(TEXT("property_value"), PropValue)) return FMCPToolResult::Error(TEXT("property_value required"));

			// v4 Phase 3: TryGet avoids LogJson warnings on missing field;
			// the existing empty-checks below handle the error path.
			TArray<TSharedPtr<FJsonValue>> Names;
			if (const TArray<TSharedPtr<FJsonValue>>* NamesPtr = nullptr;
				Args->TryGetArrayField(TEXT("actor_names"), NamesPtr) && NamesPtr)
			{
				Names = *NamesPtr;
			}
			if (Names.Num() == 0) return FMCPToolResult::Error(TEXT("No actor names provided"));

			TSet<FString> TargetNames;
			for (const auto& Val : Names)
			{
				FString Name;
				if (Val->TryGetString(Name)) TargetNames.Add(Name);
			}

			// v4 Phase 0 (bug fix): validate every target BEFORE the transaction —
			// actor must exist and expose the property. v3 applied whatever subset
			// worked and reported Success anyway; partial failures were invisible
			// and irreversible. Now: pre-validate, and roll back if any apply fails.
			TMap<FString, AActor*> Resolved;
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				if (*It && TargetNames.Contains((*It)->GetActorLabel()))
				{
					Resolved.Add((*It)->GetActorLabel(), *It);
				}
			}

			TArray<FString> PreflightErrors;
			for (const FString& Name : TargetNames)
			{
				AActor* const* Found = Resolved.Find(Name);
				if (!Found)
				{
					PreflightErrors.Add(FString::Printf(TEXT("'%s': actor not found"), *Name));
				}
				else if (!(*Found)->GetClass()->FindPropertyByName(FName(*PropName)))
				{
					PreflightErrors.Add(FString::Printf(TEXT("'%s': property '%s' not found on class %s"),
						*Name, *PropName, *(*Found)->GetClass()->GetName()));
				}
			}
			if (PreflightErrors.Num() > 0)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
					FString::Printf(TEXT("Pre-validation failed for %d of %d actors. Nothing was modified.\n%s"),
						PreflightErrors.Num(), TargetNames.Num(), *FString::Join(PreflightErrors, TEXT("\n"))),
					TEXT("Batch tools are all-or-nothing. Fix the listed items and retry."));
			}

			const int32 TxIndex = GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Batch Set Property")));

			int32 Updated = 0;
			TArray<FString> ApplyErrors;
			for (const auto& Pair : Resolved)
			{
				AActor* Actor = Pair.Value;
				FProperty* Prop = Actor->GetClass()->FindPropertyByName(FName(*PropName));

				Actor->Modify();
				void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Actor);
				if (Prop->ImportText_Direct(*PropValue, ValuePtr, Actor, PPF_None))
				{
					Actor->PostEditChange();
					Updated++;
				}
				else
				{
					ApplyErrors.Add(FString::Printf(TEXT("'%s': value '%s' could not be parsed as %s"),
						*Pair.Key, *PropValue, *Prop->GetClass()->GetName()));
				}
			}

			if (ApplyErrors.Num() > 0)
			{
				// Roll the whole batch back so the level isn't left half-updated.
				GEditor->CancelTransaction(TxIndex);
				return FMCPToolResult::ErrorStructured(EMCPError::OutOfRange,
					FString::Printf(TEXT("Apply failed on %d of %d actors — the entire batch was rolled back.\n%s"),
						ApplyErrors.Num(), Resolved.Num(), *FString::Join(ApplyErrors, TEXT("\n"))),
					TEXT("Check the value format against the property type (e.g. FVector wants 'X=0,Y=0,Z=0')."));
			}

			GEditor->EndTransaction();

			return FMCPToolResult::Success(FString::Printf(TEXT("Set '%s' = '%s' on all %d actors"), *PropName, *PropValue, Updated));
		});

	// ================================================================
	// find_actors - Advanced actor query with combined filters
	// ================================================================
	MCP_TOOL(Registry, "find_actors")
		.Description(TEXT("Advanced actor query combining class, name, tag, and proximity filters. More powerful than list_actors for targeted searches."))
		.ReadOnly()
		.Idempotent()
		.StringArg(TEXT("class_filter"), TEXT("Filter by class name (substring match)"))
		.StringArg(TEXT("name_pattern"), TEXT("Filter by actor label (substring match, case-insensitive)"))
		.StringArg(TEXT("tag"), TEXT("Filter by actor tag (exact match)"))
		.NumberArg(TEXT("near_x"), TEXT("Center X for proximity search"))
		.NumberArg(TEXT("near_y"), TEXT("Center Y for proximity search"))
		.NumberArg(TEXT("near_z"), TEXT("Center Z for proximity search"))
		.NumberArg(TEXT("radius"), TEXT("Search radius around near_x/y/z (in cm)"))
		.BoolArg(TEXT("hidden_only"), TEXT("Only return hidden actors"))
		.BoolArg(TEXT("visible_only"), TEXT("Only return visible actors"))
		.IntArg(TEXT("limit"), TEXT("Maximum results (default: 100)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
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

			return FMCPToolResult::SuccessStructured(JsonToString(Output), Output);
		});
}

} // namespace MCPBatchTools
