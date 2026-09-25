// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "MCPToolRegistry.h"
#include "MCPProtocol.h"
#include "MCPToolBuilder.h"

#include "Editor.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "CollisionQueryParams.h"
#include "Engine/OverlapResult.h"
#include "Engine/Engine.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Tools/Spatial/SpatialCommon.h"

namespace MCPSpatialTools::Bounds
{

using namespace MCPSpatialTools::Common;

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// get_actor_bounds - Get world-space bounding box of an actor
	// ================================================================
	MCP_TOOL(Registry, "get_actor_bounds")
		.Description(TEXT("Get the world-space bounding box of an actor. Returns origin, extent, min/max corners, size, and center. The fundamental tool for understanding actor dimensions."))
		.ReadOnly()
		.Idempotent()
		.StringArg(TEXT("actor_name"), TEXT("Label of the actor to get bounds for"), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			FString ActorName;
			if (!Args->TryGetStringField(TEXT("actor_name"), ActorName))
				return FMCPToolResult::Error(TEXT("actor_name is required"));

			AActor* Actor = FindActorByLabel(World, ActorName);
			if (!Actor) return FMCPToolResult::Error(FString::Printf(TEXT("Actor not found: %s"), *ActorName));

			FVector Origin, BoxExtent;
			Actor->GetActorBounds(false, Origin, BoxExtent);

			FVector Min = Origin - BoxExtent;
			FVector Max = Origin + BoxExtent;
			FVector Size = BoxExtent * 2.0;

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("actor"), ActorName);
			Result->SetStringField(TEXT("class"), Actor->GetClass()->GetName());
			Result->SetObjectField(TEXT("origin"), MakeVectorJson(Origin));
			Result->SetObjectField(TEXT("extent"), MakeVectorJson(BoxExtent));
			Result->SetObjectField(TEXT("min"), MakeVectorJson(Min));
			Result->SetObjectField(TEXT("max"), MakeVectorJson(Max));
			Result->SetObjectField(TEXT("size"), MakeVectorJson(Size));
			Result->SetObjectField(TEXT("center"), MakeVectorJson(Origin));

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});

	// ================================================================
	// get_mesh_asset_bounds - Get bounding box of a StaticMesh asset
	// ================================================================
	MCP_TOOL(Registry, "get_mesh_asset_bounds")
		.Description(TEXT("Get the bounding box of a StaticMesh ASSET (before placing it in the level). Critical for calculating how many pieces span a distance or how things fit together."))
		.ReadOnly()
		.Idempotent()
		.StringArg(TEXT("mesh_path"), TEXT("Content path of the static mesh asset (e.g., '/Game/Meshes/SM_Wall')"), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString MeshPath;
			if (!Args->TryGetStringField(TEXT("mesh_path"), MeshPath))
				return FMCPToolResult::Error(TEXT("mesh_path is required"));

			UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *MeshPath);
			if (!Mesh) return FMCPToolResult::Error(FString::Printf(TEXT("Static mesh not found: %s"), *MeshPath));

			FBox BoundingBox = Mesh->GetBoundingBox();
			FVector BoundsMin = BoundingBox.Min;
			FVector BoundsMax = BoundingBox.Max;
			FVector BoundsSize = BoundsMax - BoundsMin;
			double SphereRadius = Mesh->GetBounds().SphereRadius;

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("mesh_path"), MeshPath);
			Result->SetStringField(TEXT("mesh_name"), Mesh->GetName());
			Result->SetObjectField(TEXT("bounds_min"), MakeVectorJson(BoundsMin));
			Result->SetObjectField(TEXT("bounds_max"), MakeVectorJson(BoundsMax));
			Result->SetObjectField(TEXT("bounds_size"), MakeVectorJson(BoundsSize));
			Result->SetNumberField(TEXT("bounding_sphere_radius"), SphereRadius);

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});

} // void RegisterAll

} // namespace MCPSpatialTools::Bounds
