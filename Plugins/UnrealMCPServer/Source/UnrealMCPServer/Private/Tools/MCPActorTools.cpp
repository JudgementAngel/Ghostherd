#include "Tools/MCPActorTools.h"
#include "MCPToolRegistry.h"
#include "MCPProtocol.h"

#include "Editor.h"
#include "Engine/World.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/LightComponent.h"
#include "Engine/Selection.h"
#include "Editor/EditorEngine.h"
#include "EditorActorFolders.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/UObjectIterator.h"
#include "Engine/Engine.h"

namespace MCPActorTools
{

static UWorld* GetEditorWorld()
{
	if (GEditor)
	{
		return GEditor->GetEditorWorldContext().World();
	}
	return nullptr;
}

static FString ActorToJsonString(AActor* Actor)
{
	if (!Actor) return TEXT("null");

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetStringField(TEXT("name"), Actor->GetActorLabel());
	Obj->SetStringField(TEXT("class"), Actor->GetClass()->GetName());
	Obj->SetStringField(TEXT("path"), Actor->GetPathName());

	FVector Loc = Actor->GetActorLocation();
	FRotator Rot = Actor->GetActorRotation();
	FVector Scale = Actor->GetActorScale3D();

	TSharedPtr<FJsonObject> Transform = MakeShared<FJsonObject>();
	TSharedPtr<FJsonObject> Location = MakeShared<FJsonObject>();
	Location->SetNumberField(TEXT("x"), Loc.X);
	Location->SetNumberField(TEXT("y"), Loc.Y);
	Location->SetNumberField(TEXT("z"), Loc.Z);
	Transform->SetObjectField(TEXT("location"), Location);

	TSharedPtr<FJsonObject> Rotation = MakeShared<FJsonObject>();
	Rotation->SetNumberField(TEXT("pitch"), Rot.Pitch);
	Rotation->SetNumberField(TEXT("yaw"), Rot.Yaw);
	Rotation->SetNumberField(TEXT("roll"), Rot.Roll);
	Transform->SetObjectField(TEXT("rotation"), Rotation);

	TSharedPtr<FJsonObject> ScaleObj = MakeShared<FJsonObject>();
	ScaleObj->SetNumberField(TEXT("x"), Scale.X);
	ScaleObj->SetNumberField(TEXT("y"), Scale.Y);
	ScaleObj->SetNumberField(TEXT("z"), Scale.Z);
	Transform->SetObjectField(TEXT("scale"), ScaleObj);

	Obj->SetObjectField(TEXT("transform"), Transform);
	Obj->SetStringField(TEXT("folder"), Actor->GetFolderPath().ToString());
	Obj->SetBoolField(TEXT("hidden"), Actor->IsHidden());

	// Tags
	TArray<TSharedPtr<FJsonValue>> TagsArray;
	for (const FName& Tag : Actor->Tags)
	{
		TagsArray.Add(MakeShared<FJsonValueString>(Tag.ToString()));
	}
	Obj->SetArrayField(TEXT("tags"), TagsArray);

	return JsonToString(Obj);
}

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// list_actors - List all actors in the current level
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("class_filter"), TEXT("Filter by class name (e.g., 'StaticMeshActor', 'PointLight'). Empty = all actors."));
		FMCPSchemaBuilder::AddString(Schema, TEXT("name_filter"), TEXT("Filter by actor label (substring match, case-insensitive)"));
		FMCPSchemaBuilder::AddString(Schema, TEXT("tag_filter"), TEXT("Filter by actor tag"));
		FMCPSchemaBuilder::AddString(Schema, TEXT("folder_filter"), TEXT("Filter by folder path"));
		FMCPSchemaBuilder::AddInteger(Schema, TEXT("limit"), TEXT("Maximum number of actors to return (default: 100)"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("list_actors");
		Def.Description = TEXT("List actors in the current level with optional filtering by class, name, tag, or folder. Returns actor names, classes, transforms, and tags.");
		Def.InputSchema = Schema;
		Def.bReadOnlyHint = true;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			FString ClassFilter, NameFilter, TagFilter, FolderFilter;
			Args->TryGetStringField(TEXT("class_filter"), ClassFilter);
			Args->TryGetStringField(TEXT("name_filter"), NameFilter);
			Args->TryGetStringField(TEXT("tag_filter"), TagFilter);
			Args->TryGetStringField(TEXT("folder_filter"), FolderFilter);

			int32 Limit = 100;
			if (Args->HasField(TEXT("limit")))
			{
				Limit = FMath::Clamp((int32)Args->GetNumberField(TEXT("limit")), 1, 5000);
			}

			TArray<FString> ActorJsons;
			int32 TotalCount = 0;

			for (TActorIterator<AActor> It(World); It; ++It)
			{
				AActor* Actor = *It;
				if (!IsValid(Actor)) continue;

				// Apply filters
				if (!ClassFilter.IsEmpty() && !Actor->GetClass()->GetName().Contains(ClassFilter))
					continue;
				if (!NameFilter.IsEmpty() && !Actor->GetActorLabel().Contains(NameFilter))
					continue;
				if (!TagFilter.IsEmpty())
				{
					bool bHasTag = false;
					for (const FName& Tag : Actor->Tags)
					{
						if (Tag.ToString().Contains(TagFilter)) { bHasTag = true; break; }
					}
					if (!bHasTag) continue;
				}
				if (!FolderFilter.IsEmpty() && !Actor->GetFolderPath().ToString().Contains(FolderFilter))
					continue;

				TotalCount++;
				if (ActorJsons.Num() < Limit)
				{
					ActorJsons.Add(ActorToJsonString(Actor));
				}
			}

			FString Result = FString::Printf(TEXT("Found %d actors (showing %d):\n[%s]"),
				TotalCount, ActorJsons.Num(), *FString::Join(ActorJsons, TEXT(",\n")));

			return FMCPToolResult::Success(Result);
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// create_actor - Spawn a new actor in the level
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("actor_class"), TEXT("UE class name to spawn (e.g., 'StaticMeshActor', 'PointLight', 'CameraActor', 'PlayerStart')"), true);
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("x"), TEXT("X position (default: 0)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("y"), TEXT("Y position (default: 0)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("z"), TEXT("Z position (default: 0)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("pitch"), TEXT("Pitch rotation in degrees (default: 0)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("yaw"), TEXT("Yaw rotation in degrees (default: 0)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("roll"), TEXT("Roll rotation in degrees (default: 0)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("scale_x"), TEXT("X scale (default: 1)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("scale_y"), TEXT("Y scale (default: 1)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("scale_z"), TEXT("Z scale (default: 1)"));
		FMCPSchemaBuilder::AddString(Schema, TEXT("label"), TEXT("Actor label in the scene outliner"));
		FMCPSchemaBuilder::AddString(Schema, TEXT("folder"), TEXT("Folder path in the scene outliner"));
		FMCPSchemaBuilder::AddStringArray(Schema, TEXT("tags"), TEXT("Array of tags to apply to the actor"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("create_actor");
		Def.Description = TEXT("Spawn a new actor in the current level. Supports all standard UE actor classes including StaticMeshActor, PointLight, SpotLight, DirectionalLight, CameraActor, PlayerStart, etc.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			FString ClassName;
			if (!Args->TryGetStringField(TEXT("actor_class"), ClassName))
			{
				return FMCPToolResult::Error(TEXT("actor_class is required"));
			}

			// Find the class
			UClass* ActorClass = FindFirstObject<UClass>(*ClassName, EFindFirstObjectOptions::ExactClass);
			if (!ActorClass)
			{
				// Try with prefix
				ActorClass = FindFirstObject<UClass>(*FString::Printf(TEXT("A%s"), *ClassName), EFindFirstObjectOptions::ExactClass);
			}
			if (!ActorClass)
			{
				return FMCPToolResult::Error(FString::Printf(TEXT("Class not found: %s"), *ClassName));
			}
			if (!ActorClass->IsChildOf(AActor::StaticClass()))
			{
				return FMCPToolResult::Error(FString::Printf(TEXT("'%s' is not an Actor class"), *ClassName));
			}

			// Build transform
			FVector Location(
				Args->HasField(TEXT("x")) ? Args->GetNumberField(TEXT("x")) : 0.0,
				Args->HasField(TEXT("y")) ? Args->GetNumberField(TEXT("y")) : 0.0,
				Args->HasField(TEXT("z")) ? Args->GetNumberField(TEXT("z")) : 0.0
			);
			FRotator Rotation(
				Args->HasField(TEXT("pitch")) ? Args->GetNumberField(TEXT("pitch")) : 0.0,
				Args->HasField(TEXT("yaw")) ? Args->GetNumberField(TEXT("yaw")) : 0.0,
				Args->HasField(TEXT("roll")) ? Args->GetNumberField(TEXT("roll")) : 0.0
			);
			FVector Scale(
				Args->HasField(TEXT("scale_x")) ? Args->GetNumberField(TEXT("scale_x")) : 1.0,
				Args->HasField(TEXT("scale_y")) ? Args->GetNumberField(TEXT("scale_y")) : 1.0,
				Args->HasField(TEXT("scale_z")) ? Args->GetNumberField(TEXT("scale_z")) : 1.0
			);

			// Spawn
			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Create Actor")));

			FActorSpawnParameters SpawnParams;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

			AActor* NewActor = World->SpawnActor(ActorClass, &Location, &Rotation, SpawnParams);
			if (!NewActor)
			{
				GEditor->EndTransaction();
				return FMCPToolResult::Error(FString::Printf(TEXT("Failed to spawn actor of class %s"), *ClassName));
			}

			NewActor->SetActorScale3D(Scale);

			// Label
			FString Label;
			if (Args->TryGetStringField(TEXT("label"), Label))
			{
				NewActor->SetActorLabel(Label);
			}

			// Folder
			FString Folder;
			if (Args->TryGetStringField(TEXT("folder"), Folder))
			{
				NewActor->SetFolderPath(FName(*Folder));
			}

			// Tags
			if (Args->HasField(TEXT("tags")))
			{
				TArray<TSharedPtr<FJsonValue>> TagsArr = Args->GetArrayField(TEXT("tags"));
				for (const auto& TagVal : TagsArr)
				{
					FString TagStr;
					if (TagVal->TryGetString(TagStr))
					{
						NewActor->Tags.Add(FName(*TagStr));
					}
				}
			}

			GEditor->EndTransaction();

			return FMCPToolResult::Success(FString::Printf(TEXT("Created actor '%s' of class '%s' at (%.1f, %.1f, %.1f)"),
				*NewActor->GetActorLabel(), *ClassName, Location.X, Location.Y, Location.Z));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// destroy_actors - Delete actors by name pattern
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddStringArray(Schema, TEXT("actor_names"), TEXT("Array of actor labels to delete"), true);

		FMCPToolDefinition Def;
		Def.Name = TEXT("destroy_actors");
		Def.Description = TEXT("Delete one or more actors from the current level by their label names.");
		Def.InputSchema = Schema;
		Def.bDestructiveHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			TArray<TSharedPtr<FJsonValue>> Names = Args->GetArrayField(TEXT("actor_names"));
			if (Names.Num() == 0) return FMCPToolResult::Error(TEXT("No actor names provided"));

			TSet<FString> TargetNames;
			for (const auto& Val : Names)
			{
				FString Name;
				if (Val->TryGetString(Name)) TargetNames.Add(Name);
			}

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Destroy Actors")));

			int32 Destroyed = 0;
			TArray<AActor*> ToDestroy;

			for (TActorIterator<AActor> It(World); It; ++It)
			{
				AActor* Actor = *It;
				if (Actor && TargetNames.Contains(Actor->GetActorLabel()))
				{
					ToDestroy.Add(Actor);
				}
			}

			for (AActor* Actor : ToDestroy)
			{
				if (Actor->Destroy())
				{
					Destroyed++;
				}
			}

			GEditor->EndTransaction();

			return FMCPToolResult::Success(FString::Printf(TEXT("Destroyed %d of %d requested actors"), Destroyed, TargetNames.Num()));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// set_actor_transform - Set position/rotation/scale of an actor
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("actor_name"), TEXT("Label of the actor to transform"), true);
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
		Def.Name = TEXT("set_actor_transform");
		Def.Description = TEXT("Set or modify the transform (position, rotation, scale) of an actor. Only provided fields are changed; omitted fields keep their current value.");
		Def.InputSchema = Schema;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			FString ActorName;
			if (!Args->TryGetStringField(TEXT("actor_name"), ActorName))
				return FMCPToolResult::Error(TEXT("actor_name is required"));

			// Find actor
			AActor* TargetActor = nullptr;
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				if ((*It)->GetActorLabel() == ActorName)
				{
					TargetActor = *It;
					break;
				}
			}
			if (!TargetActor) return FMCPToolResult::Error(FString::Printf(TEXT("Actor not found: %s"), *ActorName));

			bool bRelative = false;
			Args->TryGetBoolField(TEXT("relative"), bRelative);

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Set Actor Transform")));
			TargetActor->Modify();

			FVector Loc = TargetActor->GetActorLocation();
			FRotator Rot = TargetActor->GetActorRotation();
			FVector Scale = TargetActor->GetActorScale3D();

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

			TargetActor->SetActorLocation(Loc);
			TargetActor->SetActorRotation(Rot);
			TargetActor->SetActorScale3D(Scale);

			GEditor->EndTransaction();

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Updated '%s' transform: Location(%.1f, %.1f, %.1f) Rotation(%.1f, %.1f, %.1f) Scale(%.2f, %.2f, %.2f)"),
				*ActorName, Loc.X, Loc.Y, Loc.Z, Rot.Pitch, Rot.Yaw, Rot.Roll, Scale.X, Scale.Y, Scale.Z));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// get_actor_properties - Read properties of an actor
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("actor_name"), TEXT("Label of the actor"), true);
		FMCPSchemaBuilder::AddStringArray(Schema, TEXT("property_names"), TEXT("Specific property names to read. If empty, returns all visible properties."));

		FMCPToolDefinition Def;
		Def.Name = TEXT("get_actor_properties");
		Def.Description = TEXT("Read UPROPERTY values from an actor. Returns property names, types, and values. Use without property_names to discover available properties.");
		Def.InputSchema = Schema;
		Def.bReadOnlyHint = true;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			FString ActorName;
			if (!Args->TryGetStringField(TEXT("actor_name"), ActorName))
				return FMCPToolResult::Error(TEXT("actor_name is required"));

			AActor* Actor = nullptr;
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				if ((*It)->GetActorLabel() == ActorName) { Actor = *It; break; }
			}
			if (!Actor) return FMCPToolResult::Error(FString::Printf(TEXT("Actor not found: %s"), *ActorName));

			// Collect requested property names
			TSet<FString> RequestedProps;
			if (Args->HasField(TEXT("property_names")))
			{
				for (const auto& Val : Args->GetArrayField(TEXT("property_names")))
				{
					FString PropName;
					if (Val->TryGetString(PropName)) RequestedProps.Add(PropName);
				}
			}

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("actor"), ActorName);
			Result->SetStringField(TEXT("class"), Actor->GetClass()->GetName());

			TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();

			for (TFieldIterator<FProperty> PropIt(Actor->GetClass()); PropIt; ++PropIt)
			{
				FProperty* Prop = *PropIt;
				if (!Prop) continue;
				if (!Prop->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible)) continue;

				FString PropName = Prop->GetName();
				if (RequestedProps.Num() > 0 && !RequestedProps.Contains(PropName)) continue;

				FString ValueStr;
				const void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Actor);
				Prop->ExportTextItem_Direct(ValueStr, ValuePtr, nullptr, Actor, PPF_None);

				TSharedPtr<FJsonObject> PropInfo = MakeShared<FJsonObject>();
				PropInfo->SetStringField(TEXT("type"), Prop->GetCPPType());
				PropInfo->SetStringField(TEXT("value"), ValueStr);
				PropInfo->SetStringField(TEXT("category"), Prop->GetMetaData(TEXT("Category")));
				Properties->SetObjectField(PropName, PropInfo);
			}

			Result->SetObjectField(TEXT("properties"), Properties);
			return FMCPToolResult::Success(JsonToString(Result));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// set_actor_property - Set a property on an actor
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("actor_name"), TEXT("Label of the actor"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("property_name"), TEXT("Name of the UPROPERTY to set"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("property_value"), TEXT("New value as a string (will be parsed by UE property system)"), true);

		FMCPToolDefinition Def;
		Def.Name = TEXT("set_actor_property");
		Def.Description = TEXT("Set a UPROPERTY value on an actor. The value is provided as a string and parsed by the UE property system. Use get_actor_properties first to discover property names and current values.");
		Def.InputSchema = Schema;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			FString ActorName, PropName, PropValue;
			if (!Args->TryGetStringField(TEXT("actor_name"), ActorName)) return FMCPToolResult::Error(TEXT("actor_name required"));
			if (!Args->TryGetStringField(TEXT("property_name"), PropName)) return FMCPToolResult::Error(TEXT("property_name required"));
			if (!Args->TryGetStringField(TEXT("property_value"), PropValue)) return FMCPToolResult::Error(TEXT("property_value required"));

			AActor* Actor = nullptr;
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				if ((*It)->GetActorLabel() == ActorName) { Actor = *It; break; }
			}
			if (!Actor) return FMCPToolResult::Error(FString::Printf(TEXT("Actor not found: %s"), *ActorName));

			FProperty* Prop = Actor->GetClass()->FindPropertyByName(FName(*PropName));
			if (!Prop) return FMCPToolResult::Error(FString::Printf(TEXT("Property not found: %s"), *PropName));

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Set Actor Property")));
			Actor->Modify();

			void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Actor);
			if (!Prop->ImportText_Direct(*PropValue, ValuePtr, Actor, PPF_None))
			{
				GEditor->EndTransaction();
				return FMCPToolResult::Error(FString::Printf(TEXT("Failed to parse value '%s' for property '%s'"), *PropValue, *PropName));
			}

			Actor->PostEditChange();
			GEditor->EndTransaction();

			return FMCPToolResult::Success(FString::Printf(TEXT("Set '%s.%s' = '%s'"), *ActorName, *PropName, *PropValue));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// select_actors - Set editor selection
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddStringArray(Schema, TEXT("actor_names"), TEXT("Array of actor labels to select"), true);
		FMCPSchemaBuilder::AddBoolean(Schema, TEXT("add_to_selection"), TEXT("If true, add to current selection. If false, replace selection (default: false)."));

		FMCPToolDefinition Def;
		Def.Name = TEXT("select_actors");
		Def.Description = TEXT("Select actors in the editor viewport by their labels. Useful for focusing on specific actors or preparing for batch operations.");
		Def.InputSchema = Schema;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			TArray<TSharedPtr<FJsonValue>> Names = Args->GetArrayField(TEXT("actor_names"));
			bool bAddToSelection = false;
			Args->TryGetBoolField(TEXT("add_to_selection"), bAddToSelection);

			if (!bAddToSelection)
			{
				GEditor->SelectNone(true, true, false);
			}

			TSet<FString> TargetNames;
			for (const auto& Val : Names)
			{
				FString Name;
				if (Val->TryGetString(Name)) TargetNames.Add(Name);
			}

			int32 Selected = 0;
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				AActor* Actor = *It;
				if (Actor && TargetNames.Contains(Actor->GetActorLabel()))
				{
					GEditor->SelectActor(Actor, true, true, false);
					Selected++;
				}
			}

			return FMCPToolResult::Success(FString::Printf(TEXT("Selected %d actors"), Selected));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// duplicate_actors - Clone actors with offset
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddStringArray(Schema, TEXT("actor_names"), TEXT("Array of actor labels to duplicate"), true);
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("offset_x"), TEXT("X offset from original (default: 100)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("offset_y"), TEXT("Y offset from original (default: 0)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("offset_z"), TEXT("Z offset from original (default: 0)"));
		FMCPSchemaBuilder::AddInteger(Schema, TEXT("copies"), TEXT("Number of copies to create (default: 1)"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("duplicate_actors");
		Def.Description = TEXT("Duplicate actors with an optional positional offset. Multiple copies can be created, each offset incrementally from the previous.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			TArray<TSharedPtr<FJsonValue>> Names = Args->GetArrayField(TEXT("actor_names"));
			if (Names.Num() == 0) return FMCPToolResult::Error(TEXT("No actor names provided"));

			FVector Offset(
				Args->HasField(TEXT("offset_x")) ? Args->GetNumberField(TEXT("offset_x")) : 100.0,
				Args->HasField(TEXT("offset_y")) ? Args->GetNumberField(TEXT("offset_y")) : 0.0,
				Args->HasField(TEXT("offset_z")) ? Args->GetNumberField(TEXT("offset_z")) : 0.0
			);

			int32 Copies = 1;
			if (Args->HasField(TEXT("copies")))
			{
				Copies = FMath::Clamp((int32)Args->GetNumberField(TEXT("copies")), 1, 100);
			}

			// Find actors
			TArray<AActor*> SourceActors;
			TSet<FString> TargetNames;
			for (const auto& Val : Names)
			{
				FString Name;
				if (Val->TryGetString(Name)) TargetNames.Add(Name);
			}

			for (TActorIterator<AActor> It(World); It; ++It)
			{
				if (TargetNames.Contains((*It)->GetActorLabel()))
				{
					SourceActors.Add(*It);
				}
			}

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Duplicate Actors")));

			int32 Created = 0;
			TArray<FString> CreatedNames;

			for (AActor* Source : SourceActors)
			{
				for (int32 i = 1; i <= Copies; i++)
				{
					FActorSpawnParameters SpawnParams;
					SpawnParams.Template = Source;
					SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

					FVector NewLoc = Source->GetActorLocation() + Offset * i;
					FRotator NewRot = Source->GetActorRotation();

					AActor* Clone = World->SpawnActor(Source->GetClass(), &NewLoc, &NewRot, SpawnParams);
					if (Clone)
					{
						Clone->SetActorScale3D(Source->GetActorScale3D());
						FString NewLabel = FString::Printf(TEXT("%s_Copy%d"), *Source->GetActorLabel(), i);
						Clone->SetActorLabel(NewLabel);
						CreatedNames.Add(NewLabel);
						Created++;
					}
				}
			}

			GEditor->EndTransaction();

			return FMCPToolResult::Success(FString::Printf(TEXT("Duplicated %d actors, created: %s"),
				Created, *FString::Join(CreatedNames, TEXT(", "))));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// set_actor_mobility - Change mobility of an actor
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("actor_name"), TEXT("Label of the actor"), true);
		FMCPSchemaBuilder::AddEnum(Schema, TEXT("mobility"), TEXT("Mobility setting"),
			{ TEXT("Static"), TEXT("Stationary"), TEXT("Movable") }, true);

		FMCPToolDefinition Def;
		Def.Name = TEXT("set_actor_mobility");
		Def.Description = TEXT("Set the mobility of an actor's root component (Static, Stationary, or Movable).");
		Def.InputSchema = Schema;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			FString ActorName, MobilityStr;
			if (!Args->TryGetStringField(TEXT("actor_name"), ActorName)) return FMCPToolResult::Error(TEXT("actor_name required"));
			if (!Args->TryGetStringField(TEXT("mobility"), MobilityStr)) return FMCPToolResult::Error(TEXT("mobility required"));

			AActor* Actor = nullptr;
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				if ((*It)->GetActorLabel() == ActorName) { Actor = *It; break; }
			}
			if (!Actor) return FMCPToolResult::Error(FString::Printf(TEXT("Actor not found: %s"), *ActorName));

			USceneComponent* Root = Actor->GetRootComponent();
			if (!Root) return FMCPToolResult::Error(TEXT("Actor has no root component"));

			EComponentMobility::Type Mobility;
			if (MobilityStr == TEXT("Static")) Mobility = EComponentMobility::Static;
			else if (MobilityStr == TEXT("Stationary")) Mobility = EComponentMobility::Stationary;
			else if (MobilityStr == TEXT("Movable")) Mobility = EComponentMobility::Movable;
			else return FMCPToolResult::Error(FString::Printf(TEXT("Invalid mobility: %s"), *MobilityStr));

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Set Mobility")));
			Root->Modify();
			Root->SetMobility(Mobility);
			GEditor->EndTransaction();

			return FMCPToolResult::Success(FString::Printf(TEXT("Set '%s' mobility to %s"), *ActorName, *MobilityStr));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// attach_actor - Attach one actor to another
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("actor_name"), TEXT("Label of the actor to attach (child)"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("parent_name"), TEXT("Label of the parent actor to attach to"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("socket_name"), TEXT("Optional socket name to attach to"));
		FMCPSchemaBuilder::AddEnum(Schema, TEXT("attach_rule"), TEXT("Attachment rule"),
			{ TEXT("KeepRelative"), TEXT("KeepWorld"), TEXT("SnapToTarget") });

		FMCPToolDefinition Def;
		Def.Name = TEXT("attach_actor");
		Def.Description = TEXT("Attach one actor to another as a child. The child actor will follow the parent's transform.");
		Def.InputSchema = Schema;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			FString ActorName, ParentName;
			if (!Args->TryGetStringField(TEXT("actor_name"), ActorName)) return FMCPToolResult::Error(TEXT("actor_name required"));
			if (!Args->TryGetStringField(TEXT("parent_name"), ParentName)) return FMCPToolResult::Error(TEXT("parent_name required"));

			AActor* ChildActor = nullptr;
			AActor* ParentActor = nullptr;

			for (TActorIterator<AActor> It(World); It; ++It)
			{
				FString Label = (*It)->GetActorLabel();
				if (Label == ActorName) ChildActor = *It;
				if (Label == ParentName) ParentActor = *It;
				if (ChildActor && ParentActor) break;
			}

			if (!ChildActor) return FMCPToolResult::Error(FString::Printf(TEXT("Child actor not found: %s"), *ActorName));
			if (!ParentActor) return FMCPToolResult::Error(FString::Printf(TEXT("Parent actor not found: %s"), *ParentName));

			FString SocketName;
			Args->TryGetStringField(TEXT("socket_name"), SocketName);

			FString AttachRuleStr;
			Args->TryGetStringField(TEXT("attach_rule"), AttachRuleStr);

			EAttachmentRule Rule = EAttachmentRule::KeepRelative;
			if (AttachRuleStr == TEXT("KeepWorld")) Rule = EAttachmentRule::KeepWorld;
			else if (AttachRuleStr == TEXT("SnapToTarget")) Rule = EAttachmentRule::SnapToTarget;

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Attach Actor")));
			ChildActor->Modify();

			FAttachmentTransformRules Rules(Rule, true);
			bool bAttached = ChildActor->AttachToActor(ParentActor, Rules, FName(*SocketName));

			GEditor->EndTransaction();

			if (bAttached)
			{
				return FMCPToolResult::Success(FString::Printf(TEXT("Attached '%s' to '%s'%s"),
					*ActorName, *ParentName,
					SocketName.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" (socket: %s)"), *SocketName)));
			}
			return FMCPToolResult::Error(FString::Printf(TEXT("Failed to attach '%s' to '%s'"), *ActorName, *ParentName));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// detach_actor - Detach actor from parent
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("actor_name"), TEXT("Label of the actor to detach"), true);
		FMCPSchemaBuilder::AddBoolean(Schema, TEXT("keep_world_transform"), TEXT("Keep world transform after detaching (default: true)"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("detach_actor");
		Def.Description = TEXT("Detach an actor from its parent, making it a root-level actor again.");
		Def.InputSchema = Schema;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			FString ActorName;
			if (!Args->TryGetStringField(TEXT("actor_name"), ActorName)) return FMCPToolResult::Error(TEXT("actor_name required"));

			AActor* Actor = nullptr;
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				if ((*It)->GetActorLabel() == ActorName) { Actor = *It; break; }
			}
			if (!Actor) return FMCPToolResult::Error(FString::Printf(TEXT("Actor not found: %s"), *ActorName));

			AActor* Parent = Actor->GetAttachParentActor();
			if (!Parent) return FMCPToolResult::Error(FString::Printf(TEXT("'%s' is not attached to any parent"), *ActorName));

			bool bKeepWorld = true;
			Args->TryGetBoolField(TEXT("keep_world_transform"), bKeepWorld);

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Detach Actor")));
			Actor->Modify();

			FDetachmentTransformRules Rules(bKeepWorld ? EDetachmentRule::KeepWorld : EDetachmentRule::KeepRelative, true);
			Actor->DetachFromActor(Rules);

			GEditor->EndTransaction();

			return FMCPToolResult::Success(FString::Printf(TEXT("Detached '%s' from '%s'"), *ActorName, *Parent->GetActorLabel()));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// get_actor_hierarchy - Get parent/children tree
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("actor_name"), TEXT("Label of the actor"), true);

		FMCPToolDefinition Def;
		Def.Name = TEXT("get_actor_hierarchy");
		Def.Description = TEXT("Get the parent-child hierarchy for an actor: its parent (if any) and all directly attached children.");
		Def.InputSchema = Schema;
		Def.bReadOnlyHint = true;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			FString ActorName;
			if (!Args->TryGetStringField(TEXT("actor_name"), ActorName)) return FMCPToolResult::Error(TEXT("actor_name required"));

			AActor* Actor = nullptr;
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				if ((*It)->GetActorLabel() == ActorName) { Actor = *It; break; }
			}
			if (!Actor) return FMCPToolResult::Error(FString::Printf(TEXT("Actor not found: %s"), *ActorName));

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("actor"), ActorName);
			Result->SetStringField(TEXT("class"), Actor->GetClass()->GetName());

			// Parent
			AActor* Parent = Actor->GetAttachParentActor();
			if (Parent)
			{
				TSharedPtr<FJsonObject> ParentObj = MakeShared<FJsonObject>();
				ParentObj->SetStringField(TEXT("name"), Parent->GetActorLabel());
				ParentObj->SetStringField(TEXT("class"), Parent->GetClass()->GetName());

				FName SocketName = Actor->GetAttachParentSocketName();
				if (!SocketName.IsNone())
				{
					ParentObj->SetStringField(TEXT("socket"), SocketName.ToString());
				}
				Result->SetObjectField(TEXT("parent"), ParentObj);
			}
			else
			{
				Result->SetStringField(TEXT("parent"), TEXT("None"));
			}

			// Children
			TArray<AActor*> Children;
			Actor->GetAttachedActors(Children);

			TArray<TSharedPtr<FJsonValue>> ChildArray;
			for (AActor* Child : Children)
			{
				if (!IsValid(Child)) continue;

				TSharedPtr<FJsonObject> ChildObj = MakeShared<FJsonObject>();
				ChildObj->SetStringField(TEXT("name"), Child->GetActorLabel());
				ChildObj->SetStringField(TEXT("class"), Child->GetClass()->GetName());

				FVector RelLoc = Child->GetActorLocation() - Actor->GetActorLocation();
				ChildObj->SetNumberField(TEXT("relative_x"), RelLoc.X);
				ChildObj->SetNumberField(TEXT("relative_y"), RelLoc.Y);
				ChildObj->SetNumberField(TEXT("relative_z"), RelLoc.Z);

				ChildArray.Add(MakeShared<FJsonValueObject>(ChildObj));
			}
			Result->SetArrayField(TEXT("children"), ChildArray);
			Result->SetNumberField(TEXT("child_count"), ChildArray.Num());

			return FMCPToolResult::Success(JsonToString(Result));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// set_actor_hidden - Show or hide an actor
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("actor_name"), TEXT("Label of the actor"), true);
		FMCPSchemaBuilder::AddBoolean(Schema, TEXT("hidden"), TEXT("True to hide, false to show"), true);
		FMCPSchemaBuilder::AddBoolean(Schema, TEXT("propagate_to_children"), TEXT("Apply to attached children too (default: true)"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("set_actor_hidden");
		Def.Description = TEXT("Show or hide an actor in the editor viewport. Optionally propagates to attached children.");
		Def.InputSchema = Schema;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			FString ActorName;
			bool bHidden = false;
			if (!Args->TryGetStringField(TEXT("actor_name"), ActorName)) return FMCPToolResult::Error(TEXT("actor_name required"));
			if (!Args->TryGetBoolField(TEXT("hidden"), bHidden)) return FMCPToolResult::Error(TEXT("hidden required"));

			AActor* Actor = nullptr;
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				if ((*It)->GetActorLabel() == ActorName) { Actor = *It; break; }
			}
			if (!Actor) return FMCPToolResult::Error(FString::Printf(TEXT("Actor not found: %s"), *ActorName));

			bool bPropagate = true;
			Args->TryGetBoolField(TEXT("propagate_to_children"), bPropagate);

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Set Actor Hidden")));
			Actor->Modify();
			Actor->SetIsTemporarilyHiddenInEditor(bHidden);

			if (bPropagate)
			{
				TArray<AActor*> Children;
				Actor->GetAttachedActors(Children);
				for (AActor* Child : Children)
				{
					if (IsValid(Child))
					{
						Child->Modify();
						Child->SetIsTemporarilyHiddenInEditor(bHidden);
					}
				}
			}

			GEditor->EndTransaction();

			return FMCPToolResult::Success(FString::Printf(TEXT("Set '%s' %s%s"),
				*ActorName,
				bHidden ? TEXT("hidden") : TEXT("visible"),
				bPropagate ? TEXT(" (with children)") : TEXT("")));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// set_actor_tags - Add/remove/replace tags on an actor
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("actor_name"), TEXT("Label of the actor"), true);
		FMCPSchemaBuilder::AddStringArray(Schema, TEXT("tags"), TEXT("Array of tags"), true);
		FMCPSchemaBuilder::AddEnum(Schema, TEXT("mode"), TEXT("How to apply tags"),
			{ TEXT("replace"), TEXT("add"), TEXT("remove") });

		FMCPToolDefinition Def;
		Def.Name = TEXT("set_actor_tags");
		Def.Description = TEXT("Add, remove, or replace tags on an actor. Default mode is 'replace' which overwrites all existing tags.");
		Def.InputSchema = Schema;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			FString ActorName;
			if (!Args->TryGetStringField(TEXT("actor_name"), ActorName)) return FMCPToolResult::Error(TEXT("actor_name required"));

			AActor* Actor = nullptr;
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				if ((*It)->GetActorLabel() == ActorName) { Actor = *It; break; }
			}
			if (!Actor) return FMCPToolResult::Error(FString::Printf(TEXT("Actor not found: %s"), *ActorName));

			TArray<TSharedPtr<FJsonValue>> TagValues = Args->GetArrayField(TEXT("tags"));
			TArray<FName> NewTags;
			for (const auto& Val : TagValues)
			{
				FString TagStr;
				if (Val->TryGetString(TagStr)) NewTags.Add(FName(*TagStr));
			}

			FString Mode = TEXT("replace");
			Args->TryGetStringField(TEXT("mode"), Mode);

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Set Actor Tags")));
			Actor->Modify();

			if (Mode == TEXT("add"))
			{
				for (const FName& Tag : NewTags)
				{
					Actor->Tags.AddUnique(Tag);
				}
			}
			else if (Mode == TEXT("remove"))
			{
				for (const FName& Tag : NewTags)
				{
					Actor->Tags.Remove(Tag);
				}
			}
			else // replace
			{
				Actor->Tags = NewTags;
			}

			GEditor->EndTransaction();

			// Build result tag list
			TArray<FString> TagStrs;
			for (const FName& Tag : Actor->Tags)
			{
				TagStrs.Add(Tag.ToString());
			}

			return FMCPToolResult::Success(FString::Printf(TEXT("Tags on '%s' (%s mode): [%s]"),
				*ActorName, *Mode, *FString::Join(TagStrs, TEXT(", "))));
		});
		Registry.RegisterTool(Def);
	}
}

} // namespace MCPActorTools
