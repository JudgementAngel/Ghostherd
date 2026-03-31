#include "Tools/MCPStaticMeshTools.h"
#include "MCPToolRegistry.h"
#include "MCPProtocol.h"

#include "Editor.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "EngineUtils.h"
#include "PhysicsEngine/BodySetup.h"

namespace MCPStaticMeshTools
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
	// set_static_mesh - Set mesh asset on a StaticMeshActor
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("actor_name"), TEXT("Label of the StaticMeshActor"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("mesh_path"), TEXT("Content path of the static mesh asset (e.g., '/Game/StarterContent/Shapes/Shape_Cube')"), true);

		FMCPToolDefinition Def;
		Def.Name = TEXT("set_static_mesh");
		Def.Description = TEXT("Set the static mesh asset on an actor's StaticMeshComponent. Works on any actor with a StaticMeshComponent.");
		Def.InputSchema = Schema;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			FString ActorName, MeshPath;
			if (!Args->TryGetStringField(TEXT("actor_name"), ActorName)) return FMCPToolResult::Error(TEXT("actor_name required"));
			if (!Args->TryGetStringField(TEXT("mesh_path"), MeshPath)) return FMCPToolResult::Error(TEXT("mesh_path required"));

			AActor* Actor = FindActorByLabel(World, ActorName);
			if (!Actor) return FMCPToolResult::Error(FString::Printf(TEXT("Actor not found: %s"), *ActorName));

			UStaticMeshComponent* MeshComp = Actor->FindComponentByClass<UStaticMeshComponent>();
			if (!MeshComp) return FMCPToolResult::Error(TEXT("Actor has no StaticMeshComponent"));

			UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *MeshPath);
			if (!Mesh) return FMCPToolResult::Error(FString::Printf(TEXT("Static mesh not found: %s"), *MeshPath));

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Set Static Mesh")));
			MeshComp->Modify();
			MeshComp->SetStaticMesh(Mesh);
			GEditor->EndTransaction();

			return FMCPToolResult::Success(FString::Printf(TEXT("Set mesh '%s' on actor '%s'"), *Mesh->GetName(), *ActorName));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// get_static_mesh_info - Get details about a static mesh asset
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("mesh_path"), TEXT("Content path of the static mesh asset"), true);

		FMCPToolDefinition Def;
		Def.Name = TEXT("get_static_mesh_info");
		Def.Description = TEXT("Get detailed information about a static mesh asset: vertex/triangle count, bounds, LOD count, material slots, and collision info.");
		Def.InputSchema = Schema;
		Def.bReadOnlyHint = true;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString MeshPath;
			if (!Args->TryGetStringField(TEXT("mesh_path"), MeshPath)) return FMCPToolResult::Error(TEXT("mesh_path required"));

			UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *MeshPath);
			if (!Mesh) return FMCPToolResult::Error(FString::Printf(TEXT("Static mesh not found: %s"), *MeshPath));

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("name"), Mesh->GetName());
			Result->SetStringField(TEXT("path"), Mesh->GetPathName());

			// LOD info
			int32 NumLODs = Mesh->GetNumLODs();
			Result->SetNumberField(TEXT("num_lods"), NumLODs);

			// Vertex/triangle count from LOD 0
			if (Mesh->GetRenderData() && Mesh->GetRenderData()->LODResources.Num() > 0)
			{
				const FStaticMeshLODResources& LOD0 = Mesh->GetRenderData()->LODResources[0];
				Result->SetNumberField(TEXT("vertex_count"), LOD0.GetNumVertices());
				Result->SetNumberField(TEXT("triangle_count"), LOD0.GetNumTriangles());
				Result->SetNumberField(TEXT("num_sections"), LOD0.Sections.Num());
			}

			// Bounds
			FBoxSphereBounds Bounds = Mesh->GetBounds();
			TSharedPtr<FJsonObject> BoundsObj = MakeShared<FJsonObject>();
			BoundsObj->SetNumberField(TEXT("origin_x"), Bounds.Origin.X);
			BoundsObj->SetNumberField(TEXT("origin_y"), Bounds.Origin.Y);
			BoundsObj->SetNumberField(TEXT("origin_z"), Bounds.Origin.Z);
			BoundsObj->SetNumberField(TEXT("extent_x"), Bounds.BoxExtent.X);
			BoundsObj->SetNumberField(TEXT("extent_y"), Bounds.BoxExtent.Y);
			BoundsObj->SetNumberField(TEXT("extent_z"), Bounds.BoxExtent.Z);
			BoundsObj->SetNumberField(TEXT("sphere_radius"), Bounds.SphereRadius);
			Result->SetObjectField(TEXT("bounds"), BoundsObj);

			// Material slots
			TArray<TSharedPtr<FJsonValue>> SlotsArray;
			const TArray<FStaticMaterial>& StaticMaterials = Mesh->GetStaticMaterials();
			for (int32 i = 0; i < StaticMaterials.Num(); i++)
			{
				TSharedPtr<FJsonObject> Slot = MakeShared<FJsonObject>();
				Slot->SetNumberField(TEXT("index"), i);
				Slot->SetStringField(TEXT("slot_name"), StaticMaterials[i].MaterialSlotName.ToString());
				if (StaticMaterials[i].MaterialInterface)
				{
					Slot->SetStringField(TEXT("material"), StaticMaterials[i].MaterialInterface->GetPathName());
				}
				else
				{
					Slot->SetStringField(TEXT("material"), TEXT("None"));
				}
				SlotsArray.Add(MakeShared<FJsonValueObject>(Slot));
			}
			Result->SetArrayField(TEXT("material_slots"), SlotsArray);

			// Collision info
			UBodySetup* BodySetup = Mesh->GetBodySetup();
			if (BodySetup)
			{
				TSharedPtr<FJsonObject> Collision = MakeShared<FJsonObject>();
				Collision->SetNumberField(TEXT("num_convex_elements"), BodySetup->AggGeom.ConvexElems.Num());
				Collision->SetNumberField(TEXT("num_box_elements"), BodySetup->AggGeom.BoxElems.Num());
				Collision->SetNumberField(TEXT("num_sphere_elements"), BodySetup->AggGeom.SphereElems.Num());
				Collision->SetNumberField(TEXT("num_capsule_elements"), BodySetup->AggGeom.SphylElems.Num());

				FString CollisionType;
				switch (BodySetup->CollisionTraceFlag)
				{
				case CTF_UseDefault: CollisionType = TEXT("Default"); break;
				case CTF_UseSimpleAndComplex: CollisionType = TEXT("SimpleAndComplex"); break;
				case CTF_UseSimpleAsComplex: CollisionType = TEXT("SimpleAsComplex"); break;
				case CTF_UseComplexAsSimple: CollisionType = TEXT("ComplexAsSimple"); break;
				default: CollisionType = TEXT("Unknown"); break;
				}
				Collision->SetStringField(TEXT("collision_complexity"), CollisionType);
				Result->SetObjectField(TEXT("collision"), Collision);
			}

			return FMCPToolResult::Success(JsonToString(Result));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// set_mesh_material_slots - Batch-assign materials to all slots
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("actor_name"), TEXT("Label of the actor with a StaticMeshComponent"), true);
		FMCPSchemaBuilder::AddStringArray(Schema, TEXT("materials"), TEXT("Array of material content paths, one per slot. Use empty string to skip a slot."), true);

		FMCPToolDefinition Def;
		Def.Name = TEXT("set_mesh_material_slots");
		Def.Description = TEXT("Batch-assign materials to all material slots on an actor's static mesh. Provide an array of material paths matching slot indices.");
		Def.InputSchema = Schema;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			FString ActorName;
			if (!Args->TryGetStringField(TEXT("actor_name"), ActorName)) return FMCPToolResult::Error(TEXT("actor_name required"));

			AActor* Actor = FindActorByLabel(World, ActorName);
			if (!Actor) return FMCPToolResult::Error(FString::Printf(TEXT("Actor not found: %s"), *ActorName));

			UStaticMeshComponent* MeshComp = Actor->FindComponentByClass<UStaticMeshComponent>();
			if (!MeshComp) return FMCPToolResult::Error(TEXT("Actor has no StaticMeshComponent"));

			TArray<TSharedPtr<FJsonValue>> MaterialPaths = Args->GetArrayField(TEXT("materials"));
			if (MaterialPaths.Num() == 0) return FMCPToolResult::Error(TEXT("No materials provided"));

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Set Mesh Material Slots")));
			MeshComp->Modify();

			int32 Assigned = 0;
			for (int32 i = 0; i < MaterialPaths.Num(); i++)
			{
				FString MatPath;
				if (!MaterialPaths[i]->TryGetString(MatPath) || MatPath.IsEmpty())
					continue;

				UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *MatPath);
				if (Material)
				{
					MeshComp->SetMaterial(i, Material);
					Assigned++;
				}
			}

			GEditor->EndTransaction();

			return FMCPToolResult::Success(FString::Printf(TEXT("Assigned %d materials to '%s' (%d slots provided)"),
				Assigned, *ActorName, MaterialPaths.Num()));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// create_static_mesh_actor - Spawn + set mesh + material in one call
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("mesh_path"), TEXT("Content path of the static mesh asset"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("material_path"), TEXT("Content path of a material to assign (optional)"));
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

		FMCPToolDefinition Def;
		Def.Name = TEXT("create_static_mesh_actor");
		Def.Description = TEXT("Convenience tool: spawn a StaticMeshActor, set its mesh, and optionally assign a material — all in one call.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			FString MeshPath;
			if (!Args->TryGetStringField(TEXT("mesh_path"), MeshPath)) return FMCPToolResult::Error(TEXT("mesh_path required"));

			UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *MeshPath);
			if (!Mesh) return FMCPToolResult::Error(FString::Printf(TEXT("Static mesh not found: %s"), *MeshPath));

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

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Create Static Mesh Actor")));

			FActorSpawnParameters SpawnParams;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

			AStaticMeshActor* NewActor = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Location, Rotation, SpawnParams);
			if (!NewActor)
			{
				GEditor->EndTransaction();
				return FMCPToolResult::Error(TEXT("Failed to spawn StaticMeshActor"));
			}

			NewActor->SetActorScale3D(Scale);

			UStaticMeshComponent* MeshComp = NewActor->GetStaticMeshComponent();
			if (MeshComp)
			{
				MeshComp->SetStaticMesh(Mesh);

				// Optional material
				FString MaterialPath;
				if (Args->TryGetStringField(TEXT("material_path"), MaterialPath))
				{
					UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *MaterialPath);
					if (Material)
					{
						for (int32 i = 0; i < MeshComp->GetNumMaterials(); i++)
						{
							MeshComp->SetMaterial(i, Material);
						}
					}
				}
			}

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

			GEditor->EndTransaction();

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Created StaticMeshActor '%s' with mesh '%s' at (%.1f, %.1f, %.1f)"),
				*NewActor->GetActorLabel(), *Mesh->GetName(), Location.X, Location.Y, Location.Z));
		});
		Registry.RegisterTool(Def);
	}
}

} // namespace MCPStaticMeshTools
