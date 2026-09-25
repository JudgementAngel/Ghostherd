// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "Tools/MCPActorTools.h"
#include "MCPValidate.h"
#include "Common/MCPActorResolver.h"
#include "Common/MCPPropertyIO.h"
#include "MCPToolRegistry.h"
#include "MCPToolBuilder.h"
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
	MCP_TOOL(Registry, "list_actors")
		.Description(TEXT("List actors in the current level with optional filtering by class, name, tag, or folder. Returns actor names, classes, transforms, and tags."))
		.ReadOnly()
		.Idempotent()
		.StringArg(TEXT("class_filter"), TEXT("Filter by class name (e.g., 'StaticMeshActor', 'PointLight'). Empty = all actors."))
		.StringArg(TEXT("name_filter"), TEXT("Filter by actor label (substring match, case-insensitive)"))
		.StringArg(TEXT("tag_filter"), TEXT("Filter by actor tag"))
		.StringArg(TEXT("folder_filter"), TEXT("Filter by folder path"))
		.IntArg(TEXT("limit"), TEXT("Maximum number of actors to return (default: 100)"))
		.IntArg(TEXT("offset"), TEXT("Skip this many matches before returning results — combine with limit to paginate large levels (v4)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
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
			// v4 Phase 2: offset pagination — a 50K-foliage level previously
			// truncated at 5000 with no way to reach the rest.
			int32 Offset = 0;
			if (Args->HasField(TEXT("offset")))
			{
				Offset = FMath::Max(0, (int32)Args->GetNumberField(TEXT("offset")));
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
				if (TotalCount > Offset && ActorJsons.Num() < Limit)
				{
					ActorJsons.Add(ActorToJsonString(Actor));
				}
			}

			const int32 NextOffset = Offset + ActorJsons.Num();
			FString Result = FString::Printf(TEXT("Found %d actors (showing %d, offset %d%s):\n[%s]"),
				TotalCount, ActorJsons.Num(), Offset,
				NextOffset < TotalCount ? *FString::Printf(TEXT(", next_offset %d"), NextOffset) : TEXT(""),
				*FString::Join(ActorJsons, TEXT(",\n")));

			return FMCPToolResult::Success(Result);
		});

	// ================================================================
	// create_actor - Spawn a new actor in the level
	// ================================================================
	MCP_TOOL(Registry, "create_actor")
		.Description(TEXT("Spawn a new actor in the current level. Supports all standard UE actor classes including StaticMeshActor, PointLight, SpotLight, DirectionalLight, CameraActor, PlayerStart, etc."))
		.StringArg(TEXT("actor_class"), TEXT("UE class name to spawn (e.g., 'StaticMeshActor', 'PointLight', 'CameraActor', 'PlayerStart')"), true)
		.NumberArg(TEXT("x"), TEXT("X position (default: 0)"))
		.NumberArg(TEXT("y"), TEXT("Y position (default: 0)"))
		.NumberArg(TEXT("z"), TEXT("Z position (default: 0)"))
		.NumberArg(TEXT("pitch"), TEXT("Pitch rotation in degrees (default: 0)"))
		.NumberArg(TEXT("yaw"), TEXT("Yaw rotation in degrees (default: 0)"))
		.NumberArg(TEXT("roll"), TEXT("Roll rotation in degrees (default: 0)"))
		.NumberArg(TEXT("scale_x"), TEXT("X scale (default: 1)"))
		.NumberArg(TEXT("scale_y"), TEXT("Y scale (default: 1)"))
		.NumberArg(TEXT("scale_z"), TEXT("Z scale (default: 1)"))
		.StringArg(TEXT("label"), TEXT("Actor label in the scene outliner"))
		.StringArg(TEXT("folder"), TEXT("Folder path in the scene outliner"))
		.StringArrayArg(TEXT("tags"), TEXT("Array of tags to apply to the actor"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
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

	// ================================================================
	// destroy_actors - Delete actors by name pattern
	// ================================================================
	MCP_TOOL(Registry, "destroy_actors")
		.Description(TEXT("Delete one or more actors from the current level by their label names."))
		.Destructive()
		.StringArrayArg(TEXT("actor_names"), TEXT("Array of actor labels to delete"), true)
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

	// ================================================================
	// set_actor_transform - Set position/rotation/scale of an actor
	// ================================================================
	MCP_TOOL(Registry, "set_actor_transform")
		.Description(TEXT("Set or modify the transform (position, rotation, scale) of an actor. Only provided fields are changed; omitted fields keep their current value."))
		.Idempotent()
		.StringArg(TEXT("actor_name"), TEXT("Label of the actor to transform"), true)
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

	// ================================================================
	// get_actor_properties - Read properties of an actor
	// ================================================================
	MCP_TOOL(Registry, "get_actor_properties")
		.Description(TEXT("Read UPROPERTY values from an actor. Returns property names, types, and values. Use without property_names to discover available properties."))
		.ReadOnly()
		.Idempotent()
		.StringArg(TEXT("actor_name"), TEXT("Label of the actor"), true)
		.StringArrayArg(TEXT("property_names"), TEXT("Specific property names to read. If empty, returns all visible properties."))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
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
			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});

	// ================================================================
	// set_actor_property - Set a property on an actor
	// ================================================================
	MCP_TOOL(Registry, "set_actor_property")
		.Description(TEXT("Set a UPROPERTY value on an actor. The value is provided as a string and parsed by the UE property system. Use get_actor_properties first to discover property names and current values."))
		.Idempotent()
		.StringArg(TEXT("actor_name"), TEXT("Label of the actor"), true)
		.StringArg(TEXT("property_name"), TEXT("Name of the UPROPERTY to set"), true)
		.StringArg(TEXT("property_value"), TEXT("New value as a string (will be parsed by UE property system)"), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
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

	// ================================================================
	// select_actors - Set editor selection
	// ================================================================
	MCP_TOOL(Registry, "select_actors")
		.Description(TEXT("Select actors in the editor viewport by their labels. Useful for focusing on specific actors or preparing for batch operations."))
		.Idempotent()
		.StringArrayArg(TEXT("actor_names"), TEXT("Array of actor labels to select"), true)
		.BoolArg(TEXT("add_to_selection"), TEXT("If true, add to current selection. If false, replace selection (default: false)."))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			// v4 (matrix-found): missing actor_names previously deselected
			// everything and reported success ("selected 0").
			TArray<FString> NameList;
			BAIL_IF_INVALID(FMCPValidate::RequiredStringArray(Args, TEXT("actor_names"), NameList));

			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			bool bAddToSelection = false;
			Args->TryGetBoolField(TEXT("add_to_selection"), bAddToSelection);

			if (!bAddToSelection)
			{
				GEditor->SelectNone(true, true, false);
			}

			TSet<FString> TargetNames(NameList);

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

	// ================================================================
	// duplicate_actors - Clone actors with offset
	// ================================================================
	MCP_TOOL(Registry, "duplicate_actors")
		.Description(TEXT("Duplicate actors with an optional positional offset. Multiple copies can be created, each offset incrementally from the previous."))
		.StringArrayArg(TEXT("actor_names"), TEXT("Array of actor labels to duplicate"), true)
		.NumberArg(TEXT("offset_x"), TEXT("X offset from original (default: 100)"))
		.NumberArg(TEXT("offset_y"), TEXT("Y offset from original (default: 0)"))
		.NumberArg(TEXT("offset_z"), TEXT("Z offset from original (default: 0)"))
		.IntArg(TEXT("copies"), TEXT("Number of copies to create (default: 1)"))
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

	// ================================================================
	// set_actor_mobility - Change mobility of an actor
	// ================================================================
	MCP_TOOL(Registry, "set_actor_mobility")
		.Description(TEXT("Set the mobility of an actor's root component (Static, Stationary, or Movable)."))
		.Idempotent()
		.StringArg(TEXT("actor_name"), TEXT("Label of the actor"), true)
		.EnumArg(TEXT("mobility"), TEXT("Mobility setting"),
			{ TEXT("Static"), TEXT("Stationary"), TEXT("Movable") }, true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
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

	// ================================================================
	// attach_actor - Attach one actor to another
	// ================================================================
	MCP_TOOL(Registry, "attach_actor")
		.Description(TEXT("Attach one actor to another as a child. The child actor will follow the parent's transform."))
		.Idempotent()
		.StringArg(TEXT("actor_name"), TEXT("Label of the actor to attach (child)"), true)
		.StringArg(TEXT("parent_name"), TEXT("Label of the parent actor to attach to"), true)
		.StringArg(TEXT("socket_name"), TEXT("Optional socket name to attach to"))
		.EnumArg(TEXT("attach_rule"), TEXT("Attachment rule"),
			{ TEXT("KeepRelative"), TEXT("KeepWorld"), TEXT("SnapToTarget") })
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
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

	// ================================================================
	// detach_actor - Detach actor from parent
	// ================================================================
	MCP_TOOL(Registry, "detach_actor")
		.Description(TEXT("Detach an actor from its parent, making it a root-level actor again."))
		.Idempotent()
		.StringArg(TEXT("actor_name"), TEXT("Label of the actor to detach"), true)
		.BoolArg(TEXT("keep_world_transform"), TEXT("Keep world transform after detaching (default: true)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
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

	// ================================================================
	// get_actor_hierarchy - Get parent/children tree
	// ================================================================
	MCP_TOOL(Registry, "get_actor_hierarchy")
		.Description(TEXT("Get the parent-child hierarchy for an actor: its parent (if any) and all directly attached children."))
		.ReadOnly()
		.Idempotent()
		.StringArg(TEXT("actor_name"), TEXT("Label of the actor"), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
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

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});

	// ================================================================
	// set_actor_hidden - Show or hide an actor
	// ================================================================
	MCP_TOOL(Registry, "set_actor_hidden")
		.Description(TEXT("Show or hide an actor in the editor viewport. Optionally propagates to attached children."))
		.Idempotent()
		.StringArg(TEXT("actor_name"), TEXT("Label of the actor"), true)
		.BoolArg(TEXT("hidden"), TEXT("True to hide, false to show"), true)
		.BoolArg(TEXT("propagate_to_children"), TEXT("Apply to attached children too (default: true)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
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

	// ================================================================
	// set_actor_tags - Add/remove/replace tags on an actor
	// ================================================================
	MCP_TOOL(Registry, "set_actor_tags")
		.Description(TEXT("Add, remove, or replace tags on an actor. Default mode is 'replace' which overwrites all existing tags."))
		.Idempotent()
		.StringArg(TEXT("actor_name"), TEXT("Label of the actor"), true)
		.StringArrayArg(TEXT("tags"), TEXT("Array of tags"), true)
		.EnumArg(TEXT("mode"), TEXT("How to apply tags"),
			{ TEXT("replace"), TEXT("add"), TEXT("remove") })
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
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

	// ================================================================
	// list_actor_components — scene legibility (v4 Phase 2)
	// ================================================================
	MCP_TOOL(Registry, "list_actor_components")
		.Description(TEXT("List an actor's full component tree: name, class, attach parent, and (for scene components) relative transform. Use before set_component_property to discover component names."))
		.ReadOnly()
		.Idempotent()
		.StringArg(TEXT("actor_name"), TEXT("Actor label"), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			FString ActorName;
			if (!Args->TryGetStringField(TEXT("actor_name"), ActorName))
				return FMCPToolResult::Error(TEXT("actor_name required"));

			AActor* Actor = MCPCommon::FindActorByLabel(World, ActorName);
			if (!Actor)
				return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
					FString::Printf(TEXT("Actor not found: %s"), *ActorName));

			TArray<TSharedPtr<FJsonValue>> CompArr;
			for (UActorComponent* Comp : Actor->GetComponents())
			{
				if (!Comp) continue;
				TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("name"), Comp->GetName());
				Entry->SetStringField(TEXT("class"), Comp->GetClass()->GetName());
				if (const USceneComponent* Scene = Cast<USceneComponent>(Comp))
				{
					if (Scene->GetAttachParent())
					{
						Entry->SetStringField(TEXT("attach_parent"), Scene->GetAttachParent()->GetName());
					}
					const FVector Loc = Scene->GetRelativeLocation();
					const FRotator Rot = Scene->GetRelativeRotation();
					const FVector Scale = Scene->GetRelativeScale3D();
					TSharedPtr<FJsonObject> Xform = MakeShared<FJsonObject>();
					Xform->SetNumberField(TEXT("x"), Loc.X);     Xform->SetNumberField(TEXT("y"), Loc.Y);     Xform->SetNumberField(TEXT("z"), Loc.Z);
					Xform->SetNumberField(TEXT("pitch"), Rot.Pitch); Xform->SetNumberField(TEXT("yaw"), Rot.Yaw); Xform->SetNumberField(TEXT("roll"), Rot.Roll);
					Xform->SetNumberField(TEXT("scale_x"), Scale.X); Xform->SetNumberField(TEXT("scale_y"), Scale.Y); Xform->SetNumberField(TEXT("scale_z"), Scale.Z);
					Entry->SetObjectField(TEXT("relative_transform"), Xform);
				}
				CompArr.Add(MakeShared<FJsonValueObject>(Entry));
			}

			TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
			Out->SetStringField(TEXT("actor"), ActorName);
			Out->SetNumberField(TEXT("count"), CompArr.Num());
			Out->SetArrayField(TEXT("components"), CompArr);
			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("'%s' has %d component(s)."), *ActorName, CompArr.Num()), Out);
		});

	// ================================================================
	// get_component_info — per-component property dump (v4 Phase 2)
	// ================================================================
	MCP_TOOL(Registry, "get_component_info")
		.Description(TEXT("Inspect one component on an actor: class hierarchy and the values of requested properties (or a curated default set). Property values are exported as JSON."))
		.ReadOnly()
		.Idempotent()
		.StringArg(TEXT("actor_name"), TEXT("Actor label"), true)
		.StringArg(TEXT("component_name"), TEXT("Component name (from list_actor_components)"), true)
		.StringArrayArg(TEXT("properties"), TEXT("Specific property names to read (omit for common ones: Mobility, bVisible, bHiddenInGame, ComponentTags)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			FString ActorName, CompName;
			if (!Args->TryGetStringField(TEXT("actor_name"), ActorName))
				return FMCPToolResult::Error(TEXT("actor_name required"));
			if (!Args->TryGetStringField(TEXT("component_name"), CompName))
				return FMCPToolResult::Error(TEXT("component_name required"));

			AActor* Actor = MCPCommon::FindActorByLabel(World, ActorName);
			if (!Actor)
				return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
					FString::Printf(TEXT("Actor not found: %s"), *ActorName));

			UActorComponent* Target = nullptr;
			TArray<FString> Available;
			for (UActorComponent* Comp : Actor->GetComponents())
			{
				if (!Comp) continue;
				Available.Add(Comp->GetName());
				if (Comp->GetName() == CompName) { Target = Comp; }
			}
			if (!Target)
				return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
					FString::Printf(TEXT("Component '%s' not found on '%s'"), *CompName, *ActorName),
					TEXT("Component names are listed in did_you_mean."), Available);

			TArray<FString> PropNames;
			const TArray<TSharedPtr<FJsonValue>>* Requested = nullptr;
			if (Args->TryGetArrayField(TEXT("properties"), Requested))
			{
				for (const auto& V : *Requested) { FString N; if (V->TryGetString(N)) PropNames.Add(N); }
			}
			if (PropNames.Num() == 0)
			{
				PropNames = { TEXT("Mobility"), TEXT("bVisible"), TEXT("bHiddenInGame"), TEXT("ComponentTags") };
			}

			TSharedPtr<FJsonObject> Props = MakeShared<FJsonObject>();
			TArray<FString> Missing;
			for (const FString& PropName : PropNames)
			{
				FProperty* Prop = Target->GetClass()->FindPropertyByName(FName(*PropName));
				if (!Prop) { Missing.Add(PropName); continue; }
				TSharedPtr<FJsonValue> Value = MCPCommon::ExportPropertyToJson(Prop, Target);
				Props->SetField(PropName, Value.IsValid() ? Value : MakeShared<FJsonValueNull>());
			}

			TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
			Out->SetStringField(TEXT("component"), CompName);
			Out->SetStringField(TEXT("class"), Target->GetClass()->GetName());
			// Class hierarchy up to UActorComponent for capability discovery.
			TArray<TSharedPtr<FJsonValue>> Hierarchy;
			for (UClass* C = Target->GetClass(); C && C != UActorComponent::StaticClass()->GetSuperClass(); C = C->GetSuperClass())
			{
				Hierarchy.Add(MakeShared<FJsonValueString>(C->GetName()));
				if (C == UActorComponent::StaticClass()) break;
			}
			Out->SetArrayField(TEXT("class_hierarchy"), Hierarchy);
			Out->SetObjectField(TEXT("properties"), Props);
			if (Missing.Num() > 0)
			{
				TArray<TSharedPtr<FJsonValue>> MissArr;
				for (const FString& M : Missing) MissArr.Add(MakeShared<FJsonValueString>(M));
				Out->SetArrayField(TEXT("missing_properties"), MissArr);
			}
			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Component '%s' (%s) on '%s'."), *CompName,
					*Target->GetClass()->GetName(), *ActorName), Out);
		});
}

} // namespace MCPActorTools
