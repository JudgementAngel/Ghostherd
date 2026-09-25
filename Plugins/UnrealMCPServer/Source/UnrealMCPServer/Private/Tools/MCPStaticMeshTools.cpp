// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "Tools/MCPStaticMeshTools.h"
#include "Common/MCPActorResolver.h"
#include "MCPToolRegistry.h"
#include "MCPToolBuilder.h"
#include "MCPProtocol.h"

#include "Editor.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "EngineUtils.h"
#include "PhysicsEngine/BodySetup.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "Interfaces/ITargetPlatform.h"
#include "MeshDescription.h"

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
	// v4 Phase 1: cached resolver (O(1) amortized) replaces the per-call actor scan.
	return MCPCommon::FindActorByLabel(World, Label);
}

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// set_static_mesh - Set mesh asset on a StaticMeshActor
	// ================================================================
	MCP_TOOL(Registry, "set_static_mesh")
		.Description(TEXT("Set the static mesh asset on an actor's StaticMeshComponent. Works on any actor with a StaticMeshComponent."))
		.Idempotent()
		.StringArg(TEXT("actor_name"), TEXT("Label of the StaticMeshActor"), true)
		.StringArg(TEXT("mesh_path"), TEXT("Content path of the static mesh asset (e.g., '/Game/StarterContent/Shapes/Shape_Cube')"), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
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

	// ================================================================
	// get_static_mesh_info - Get details about a static mesh asset
	// ================================================================
	MCP_TOOL(Registry, "get_static_mesh_info")
		.Description(TEXT("Get detailed information about a static mesh asset: vertex/triangle count, bounds, LOD count, material slots, and collision info."))
		.ReadOnly()
		.Idempotent()
		.StringArg(TEXT("mesh_path"), TEXT("Content path of the static mesh asset"), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
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

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});

	// ================================================================
	// set_mesh_material_slots - Batch-assign materials to all slots
	// ================================================================
	MCP_TOOL(Registry, "set_mesh_material_slots")
		.Description(TEXT("Batch-assign materials to all material slots on an actor's static mesh. Provide an array of material paths matching slot indices."))
		.Idempotent()
		.StringArg(TEXT("actor_name"), TEXT("Label of the actor with a StaticMeshComponent"), true)
		.StringArrayArg(TEXT("materials"), TEXT("Array of material content paths, one per slot. Use empty string to skip a slot."), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
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

	// ================================================================
	// create_static_mesh_actor - Spawn + set mesh + material in one call
	// ================================================================
	MCP_TOOL(Registry, "create_static_mesh_actor")
		.Description(TEXT("Convenience tool: spawn a StaticMeshActor, set its mesh, and optionally assign a material — all in one call."))
		.StringArg(TEXT("mesh_path"), TEXT("Content path of the static mesh asset"), true)
		.StringArg(TEXT("material_path"), TEXT("Content path of a material to assign (optional)"))
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
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
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

	// ================================================================
	// configure_mesh_lod - Set LOD count, screen sizes, reduction
	// ================================================================
	MCP_TOOL(Registry, "configure_mesh_lod")
		.Description(TEXT("Configure LOD (Level of Detail) settings on a static mesh. Set the number of auto-generated LODs, screen size thresholds for each LOD transition, and triangle reduction ratio. LODs reduce rendering cost by showing simpler meshes at distance. Use get_mesh_complexity_report to see current LOD state."))
		.Idempotent()
		.StringArg(TEXT("asset_path"), TEXT("Content path to the StaticMesh asset"), true)
		.IntArg(TEXT("lod_count"), TEXT("Number of LODs to generate (2-8). LOD0 is the original mesh."))
		.BoolArg(TEXT("auto_compute_lod_distances"), TEXT("Auto-compute screen sizes for each LOD (default: true)"))
		.StringArg(TEXT("screen_sizes_json"), TEXT("JSON array of screen size thresholds per LOD, e.g. '[1.0, 0.5, 0.25, 0.1]'. LOD0=1.0 means full size."))
		.NumberArg(TEXT("reduction_percent_per_lod"), TEXT("Triangle reduction percentage per LOD level (default: 50, meaning each LOD has 50%% of previous)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));

			UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *AssetPath);
			if (!Mesh)
				return FMCPToolResult::Error(FString::Printf(TEXT("StaticMesh not found: %s"), *AssetPath));

			Mesh->PreEditChange(nullptr);

			TArray<FString> Changes;

			// LOD count / auto-generation
			int32 LODCount = 0;
			if (Args->HasField(TEXT("lod_count")))
			{
				LODCount = FMath::Clamp((int32)Args->GetNumberField(TEXT("lod_count")), 2, 8);

				// Set up LOD group settings for auto-generation
				ITargetPlatformManagerModule& TargetPlatformManager = GetTargetPlatformManagerRef();
				ITargetPlatform* RunningPlatform = TargetPlatformManager.GetRunningTargetPlatform();

				if (RunningPlatform)
				{
					FStaticMeshSourceModel* SourceModels = nullptr;
					int32 CurrentLODCount = Mesh->GetNumSourceModels();

					// Add source models up to desired LOD count
					while (Mesh->GetNumSourceModels() < LODCount)
					{
						Mesh->AddSourceModel();
					}

					// Configure reduction settings for each new LOD
					float ReductionPercent = 50.0f;
					if (Args->HasField(TEXT("reduction_percent_per_lod")))
						ReductionPercent = FMath::Clamp((float)Args->GetNumberField(TEXT("reduction_percent_per_lod")), 10.0f, 90.0f);

					for (int32 LODIdx = 1; LODIdx < LODCount; LODIdx++)
					{
						FStaticMeshSourceModel& SourceModel = Mesh->GetSourceModel(LODIdx);
						FMeshReductionSettings& ReductionSettings = SourceModel.ReductionSettings;

						// Each LOD reduces by the given percentage from the original
						float PercentTriangles = FMath::Pow((100.0f - ReductionPercent) / 100.0f, (float)LODIdx);
						ReductionSettings.PercentTriangles = PercentTriangles;
						SourceModel.BuildSettings.bRecomputeNormals = false;
						SourceModel.BuildSettings.bRecomputeTangents = false;
					}

					Changes.Add(FString::Printf(TEXT("LOD count=%d, reduction=%.0f%% per level"), LODCount, ReductionPercent));
				}
			}

			// Screen sizes
			FString ScreenSizesJson;
			if (Args->TryGetStringField(TEXT("screen_sizes_json"), ScreenSizesJson))
			{
				TArray<TSharedPtr<FJsonValue>> ScreenSizes;
				TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ScreenSizesJson);
				if (FJsonSerializer::Deserialize(Reader, ScreenSizes))
				{
					for (int32 i = 0; i < ScreenSizes.Num() && i < Mesh->GetNumSourceModels(); i++)
					{
						float ScreenSize = (float)ScreenSizes[i]->AsNumber();
						Mesh->GetSourceModel(i).ScreenSize = FPerPlatformFloat(ScreenSize);
					}
					Changes.Add(FString::Printf(TEXT("Screen sizes set for %d LODs"), ScreenSizes.Num()));
				}
			}
			else
			{
				// Auto-compute screen sizes if requested
				bool bAutoCompute = true;
				Args->TryGetBoolField(TEXT("auto_compute_lod_distances"), bAutoCompute);
				if (bAutoCompute && LODCount > 0)
				{
					for (int32 i = 0; i < Mesh->GetNumSourceModels(); i++)
					{
						float ScreenSize = 1.0f / FMath::Pow(2.0f, (float)i);
						Mesh->GetSourceModel(i).ScreenSize = FPerPlatformFloat(ScreenSize);
					}
					Changes.Add(TEXT("Auto-computed screen sizes"));
				}
			}

			if (Changes.Num() == 0)
				return FMCPToolResult::Success(TEXT("No LOD settings changed. Provide lod_count, screen_sizes_json, or other parameters."));

			Mesh->PostEditChange();
			Mesh->MarkPackageDirty();

			// Build mesh to apply LOD changes
			Mesh->Build();

			// Save
			FString PackagePath = FPackageName::ObjectPathToPackageName(Mesh->GetPathName());
			FString PackageFilename = FPackageName::LongPackageNameToFilename(PackagePath, FPackageName::GetAssetPackageExtension());
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
			UPackage::SavePackage(Mesh->GetPackage(), Mesh, *PackageFilename, SaveArgs);

			return FMCPToolResult::Success(FString::Printf(TEXT("Configured LODs on '%s': %s. Final LOD count: %d"),
				*Mesh->GetName(), *FString::Join(Changes, TEXT(", ")), Mesh->GetNumLODs()));
		});

	// ================================================================
	// enable_nanite - Enable/disable Nanite on a static mesh
	// ================================================================
	MCP_TOOL(Registry, "enable_nanite")
		.Description(TEXT("Enable or disable Nanite virtualized geometry on a static mesh asset. Nanite provides automatic LOD with virtually unlimited polygon counts. The mesh is saved after modification."))
		.Idempotent()
		.StringArg(TEXT("asset_path"), TEXT("Content path to the StaticMesh asset"), true)
		.BoolArg(TEXT("enabled"), TEXT("True to enable Nanite, false to disable"), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));

			bool bEnabled = true;
			if (!Args->TryGetBoolField(TEXT("enabled"), bEnabled))
				return FMCPToolResult::Error(TEXT("enabled is required"));

			UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *AssetPath);
			if (!Mesh)
				return FMCPToolResult::Error(FString::Printf(TEXT("StaticMesh not found: %s"), *AssetPath));

			Mesh->PreEditChange(nullptr);
			FMeshNaniteSettings NaniteSettings = Mesh->GetNaniteSettings();
			NaniteSettings.bEnabled = bEnabled;
			Mesh->SetNaniteSettings(NaniteSettings);
			Mesh->PostEditChange();
			Mesh->MarkPackageDirty();

			// Save
			FString PackagePath = FPackageName::ObjectPathToPackageName(Mesh->GetPathName());
			FString PackageFilename = FPackageName::LongPackageNameToFilename(PackagePath, FPackageName::GetAssetPackageExtension());
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
			UPackage::SavePackage(Mesh->GetPackage(), Mesh, *PackageFilename, SaveArgs);

			return FMCPToolResult::Success(FString::Printf(TEXT("Nanite %s on mesh '%s'"),
				bEnabled ? TEXT("enabled") : TEXT("disabled"), *Mesh->GetName()));
		});

	// ================================================================
	// get_mesh_complexity_report - Mesh complexity analysis
	// ================================================================
	MCP_TOOL(Registry, "get_mesh_complexity_report")
		.Description(TEXT("Get detailed complexity report for a static mesh: triangle count, vertex count, LOD count, screen sizes, Nanite state, material slot count, collision complexity, and estimated VRAM usage."))
		.ReadOnly()
		.Idempotent()
		.StringArg(TEXT("asset_path"), TEXT("Content path to the StaticMesh asset"), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));

			UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *AssetPath);
			if (!Mesh)
				return FMCPToolResult::Error(FString::Printf(TEXT("StaticMesh not found: %s"), *AssetPath));

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("name"), Mesh->GetName());
			Result->SetStringField(TEXT("path"), AssetPath);

			// Nanite state
			Result->SetBoolField(TEXT("nanite_enabled"), Mesh->GetNaniteSettings().bEnabled);

			// LODs
			int32 LODCount = Mesh->GetNumLODs();
			Result->SetNumberField(TEXT("lod_count"), LODCount);

			TArray<TSharedPtr<FJsonValue>> LODArray;
			int64 TotalTriangles = 0;
			int64 TotalVertices = 0;

			for (int32 LODIdx = 0; LODIdx < LODCount; LODIdx++)
			{
				const FStaticMeshLODResources& LODRes = Mesh->GetRenderData()->LODResources[LODIdx];
				int32 Tris = LODRes.GetNumTriangles();
				int32 Verts = LODRes.GetNumVertices();

				TSharedPtr<FJsonObject> LODObj = MakeShared<FJsonObject>();
				LODObj->SetNumberField(TEXT("lod_index"), LODIdx);
				LODObj->SetNumberField(TEXT("triangles"), Tris);
				LODObj->SetNumberField(TEXT("vertices"), Verts);
				LODObj->SetNumberField(TEXT("sections"), LODRes.Sections.Num());

				if (LODIdx == 0)
				{
					TotalTriangles = Tris;
					TotalVertices = Verts;
				}

				LODArray.Add(MakeShared<FJsonValueObject>(LODObj));
			}

			Result->SetArrayField(TEXT("lods"), LODArray);
			Result->SetNumberField(TEXT("total_triangles_lod0"), TotalTriangles);
			Result->SetNumberField(TEXT("total_vertices_lod0"), TotalVertices);
			Result->SetNumberField(TEXT("material_slots"), Mesh->GetStaticMaterials().Num());

			// Bounds
			FBoxSphereBounds Bounds = Mesh->GetBounds();
			Result->SetNumberField(TEXT("bound_radius"), Bounds.SphereRadius);

			// Collision
			bool bHasCollision = Mesh->GetBodySetup() != nullptr;
			Result->SetBoolField(TEXT("has_collision"), bHasCollision);

			// Complexity warnings
			TArray<FString> Warnings;
			if (TotalTriangles > 100000 && !Mesh->GetNaniteSettings().bEnabled)
				Warnings.Add(FString::Printf(TEXT("High poly mesh (%lld tris) without Nanite. Consider enabling Nanite."), TotalTriangles));
			if (LODCount <= 1 && TotalTriangles > 10000 && !Mesh->GetNaniteSettings().bEnabled)
				Warnings.Add(TEXT("No LODs on a high-poly mesh. Add LODs or enable Nanite."));
			if (Mesh->GetStaticMaterials().Num() > 8)
				Warnings.Add(FString::Printf(TEXT("High material slot count: %d. Each slot is a separate draw call."), Mesh->GetStaticMaterials().Num()));

			if (Warnings.Num() > 0)
			{
				TArray<TSharedPtr<FJsonValue>> WarnArray;
				for (const FString& W : Warnings)
					WarnArray.Add(MakeShared<FJsonValueString>(W));
				Result->SetArrayField(TEXT("warnings"), WarnArray);
			}

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});
}

} // namespace MCPStaticMeshTools
