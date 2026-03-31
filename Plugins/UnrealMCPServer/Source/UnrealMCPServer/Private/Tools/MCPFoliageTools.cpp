#include "Tools/MCPFoliageTools.h"
#include "MCPToolRegistry.h"
#include "MCPProtocol.h"

#include "FoliageType_InstancedStaticMesh.h"
#include "InstancedFoliageActor.h"
#include "FoliageType.h"
#include "Engine/StaticMesh.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "CollisionQueryParams.h"

namespace MCPFoliageTools
{

static UWorld* GetEditorWorld()
{
	if (GEditor)
	{
		return GEditor->GetEditorWorldContext().World();
	}
	return nullptr;
}

// ---------------------------------------------------------------------------
// Helper: Get or create the InstancedFoliageActor for the current level.
// ---------------------------------------------------------------------------
static AInstancedFoliageActor* GetOrCreateFoliageActor(UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}

	return AInstancedFoliageActor::GetInstancedFoliageActorForCurrentLevel(
		World, /*bCreateIfNone=*/true);
}

// ---------------------------------------------------------------------------
// Helper: Find the UFoliageType_InstancedStaticMesh registered for a given
// UStaticMesh inside a foliage actor. Returns nullptr if not found.
// Uses the public GetFoliageInfos() accessor because FoliageInfos is private.
// ---------------------------------------------------------------------------
static UFoliageType_InstancedStaticMesh* FindFoliageTypeForMesh(
	AInstancedFoliageActor* FoliageActor,
	UStaticMesh* Mesh)
{
	if (!FoliageActor || !Mesh)
	{
		return nullptr;
	}

	for (const auto& Pair : FoliageActor->GetFoliageInfos())
	{
		UFoliageType* FoliageType = Pair.Key;
		UFoliageType_InstancedStaticMesh* ISMType =
			Cast<UFoliageType_InstancedStaticMesh>(FoliageType);
		if (ISMType && ISMType->GetStaticMesh() == Mesh)
		{
			return ISMType;
		}
	}
	return nullptr;
}

// ---------------------------------------------------------------------------
// Helper: Trace straight down from (X, Y, StartZ) to find the ground surface.
// Returns true and sets HitZ when a hit is found; otherwise leaves HitZ as
// StartZ so instances still appear at a sensible location.
// ---------------------------------------------------------------------------
static bool TraceGroundZ(UWorld* World, float X, float Y, float StartZ, float& HitZ)
{
	HitZ = StartZ;

	const FVector TraceStart(X, Y, StartZ + 5000.0f);
	const FVector TraceEnd(X, Y, StartZ - 5000.0f);

	FHitResult Hit;
	FCollisionQueryParams QueryParams;
	QueryParams.bTraceComplex = false;

	if (World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_WorldStatic, QueryParams))
	{
		HitZ = Hit.ImpactPoint.Z;
		return true;
	}
	return false;
}

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// add_foliage_type - Register a static mesh as a foliage type
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("mesh_path"),
			TEXT("Content path to the StaticMesh asset (e.g., '/Game/Foliage/SM_Tree')"),
			/*bRequired=*/true);

		FMCPToolDefinition Def;
		Def.Name = TEXT("add_foliage_type");
		Def.Description = TEXT(
			"Register a static mesh as a foliage type on the level's InstancedFoliageActor. "
			"If a foliage type for this mesh already exists it is returned unchanged. "
			"Use paint_foliage afterwards to scatter instances.");
		Def.InputSchema = Schema;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World)
			{
				return FMCPToolResult::Error(TEXT("No editor world available"));
			}

			FString MeshPath;
			if (!Args->TryGetStringField(TEXT("mesh_path"), MeshPath))
			{
				return FMCPToolResult::Error(TEXT("mesh_path is required"));
			}

			UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *MeshPath);
			if (!IsValid(Mesh))
			{
				return FMCPToolResult::Error(
					FString::Printf(TEXT("Static mesh not found: %s"), *MeshPath));
			}

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Add Foliage Type")));

			AInstancedFoliageActor* FoliageActor = GetOrCreateFoliageActor(World);
			if (!FoliageActor)
			{
				GEditor->EndTransaction();
				return FMCPToolResult::Error(
					TEXT("Failed to get or create InstancedFoliageActor"));
			}
			FoliageActor->Modify();

			// Return existing type if already registered for this mesh
			UFoliageType_InstancedStaticMesh* ExistingType =
				FindFoliageTypeForMesh(FoliageActor, Mesh);
			if (ExistingType)
			{
				GEditor->EndTransaction();
				return FMCPToolResult::Success(
					FString::Printf(
						TEXT("Foliage type for '%s' already registered (type: %s)"),
						*Mesh->GetName(),
						*ExistingType->GetName()));
			}

			// AddMesh is the safest public API: it creates + registers a local
			// UFoliageType_InstancedStaticMesh owned by the foliage actor.
			UFoliageType* OutSettings = nullptr;
			FFoliageInfo* FoliageInfo = FoliageActor->AddMesh(Mesh, &OutSettings);
			if (!OutSettings || !FoliageInfo)
			{
				GEditor->EndTransaction();
				return FMCPToolResult::Error(
					TEXT("AddMesh failed; could not register foliage type"));
			}

			GEditor->EndTransaction();

			return FMCPToolResult::Success(
				FString::Printf(
					TEXT("Added foliage type for mesh '%s' (type object: %s)"),
					*Mesh->GetName(),
					*OutSettings->GetName()));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// paint_foliage - Scatter foliage instances at a location
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("mesh_path"),
			TEXT("Content path to the StaticMesh used as the foliage type"),
			/*bRequired=*/true);
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("x"),
			TEXT("Center X position in world space"),
			/*bRequired=*/true);
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("y"),
			TEXT("Center Y position in world space"),
			/*bRequired=*/true);
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("z"),
			TEXT("Center Z position (used as trace start height; ground is found via line trace)"),
			/*bRequired=*/true);
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("radius"),
			TEXT("Scatter radius in cm around the center point (default: 500)"));
		FMCPSchemaBuilder::AddInteger(Schema, TEXT("count"),
			TEXT("Number of instances to add (default: 10)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("random_scale_min"),
			TEXT("Minimum uniform random scale applied to each instance (default: 0.8)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("random_scale_max"),
			TEXT("Maximum uniform random scale applied to each instance (default: 1.2)"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("paint_foliage");
		Def.Description = TEXT(
			"Scatter foliage instances randomly within a radius around a world-space center. "
			"The foliage type is found or created automatically from the given mesh. "
			"A downward line trace finds the ground surface so instances sit correctly on "
			"terrain or geometry.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World)
			{
				return FMCPToolResult::Error(TEXT("No editor world available"));
			}

			FString MeshPath;
			if (!Args->TryGetStringField(TEXT("mesh_path"), MeshPath))
			{
				return FMCPToolResult::Error(TEXT("mesh_path is required"));
			}

			if (!Args->HasField(TEXT("x")) || !Args->HasField(TEXT("y")) ||
			    !Args->HasField(TEXT("z")))
			{
				return FMCPToolResult::Error(TEXT("x, y, and z are required"));
			}

			const float CenterX  = (float)Args->GetNumberField(TEXT("x"));
			const float CenterY  = (float)Args->GetNumberField(TEXT("y"));
			const float CenterZ  = (float)Args->GetNumberField(TEXT("z"));
			const float Radius   = Args->HasField(TEXT("radius"))
			                       ? (float)Args->GetNumberField(TEXT("radius"))
			                       : 500.0f;
			const int32 Count    = Args->HasField(TEXT("count"))
			                       ? FMath::Clamp((int32)Args->GetNumberField(TEXT("count")), 1, 10000)
			                       : 10;
			float ScaleMin       = Args->HasField(TEXT("random_scale_min"))
			                       ? (float)Args->GetNumberField(TEXT("random_scale_min"))
			                       : 0.8f;
			float ScaleMax       = Args->HasField(TEXT("random_scale_max"))
			                       ? (float)Args->GetNumberField(TEXT("random_scale_max"))
			                       : 1.2f;

			if (ScaleMin > ScaleMax)
			{
				ScaleMax = ScaleMin;
			}

			UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *MeshPath);
			if (!IsValid(Mesh))
			{
				return FMCPToolResult::Error(
					FString::Printf(TEXT("Static mesh not found: %s"), *MeshPath));
			}

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Paint Foliage")));

			AInstancedFoliageActor* FoliageActor = GetOrCreateFoliageActor(World);
			if (!FoliageActor)
			{
				GEditor->EndTransaction();
				return FMCPToolResult::Error(
					TEXT("Failed to get or create InstancedFoliageActor"));
			}
			FoliageActor->Modify();

			// Find or create the foliage type for this mesh
			UFoliageType_InstancedStaticMesh* FoliageType =
				FindFoliageTypeForMesh(FoliageActor, Mesh);

			FFoliageInfo* FoliageInfo = nullptr;

			if (!FoliageType)
			{
				// AddMesh creates a new local foliage type and its FFoliageInfo
				UFoliageType* OutSettings = nullptr;
				FoliageInfo = FoliageActor->AddMesh(Mesh, &OutSettings);
				FoliageType = Cast<UFoliageType_InstancedStaticMesh>(OutSettings);
			}
			else
			{
				// FindOrAddMesh returns (or creates) the FFoliageInfo for an
				// existing foliage type
				FoliageInfo = FoliageActor->FindOrAddMesh(FoliageType);
			}

			if (!FoliageType || !FoliageInfo)
			{
				GEditor->EndTransaction();
				return FMCPToolResult::Error(
					TEXT("Failed to acquire foliage type or FFoliageInfo"));
			}

			// Scatter instances using rejection sampling inside a circle
			FRandomStream RandStream(FMath::Rand());
			int32 Added = 0;

			for (int32 i = 0; i < Count; ++i)
			{
				float OffsetX = 0.0f;
				float OffsetY = 0.0f;
				int32 Attempts = 0;
				do
				{
					OffsetX = RandStream.FRandRange(-Radius, Radius);
					OffsetY = RandStream.FRandRange(-Radius, Radius);
					Attempts++;
				}
				while ((OffsetX * OffsetX + OffsetY * OffsetY) > (Radius * Radius)
				       && Attempts < 20);

				const float InstanceX = CenterX + OffsetX;
				const float InstanceY = CenterY + OffsetY;
				float InstanceZ = CenterZ;

				// Attempt a downward line trace to find the actual surface Z
				TraceGroundZ(World, InstanceX, InstanceY, CenterZ, InstanceZ);

				const float UniformScale = RandStream.FRandRange(ScaleMin, ScaleMax);
				const float RandomYaw    = RandStream.FRandRange(0.0f, 360.0f);

				FFoliageInstance Instance;
				Instance.Location    = FVector(InstanceX, InstanceY, InstanceZ);
				Instance.Rotation    = FRotator(0.0f, RandomYaw, 0.0f);
				// FFoliageInstancePlacementInfo::DrawScale3D is FVector3f in UE5
				Instance.DrawScale3D = FVector3f(UniformScale, UniformScale, UniformScale);

				FoliageInfo->AddInstance(FoliageType, Instance);
				Added++;
			}

			FoliageInfo->Refresh(/*bAsync=*/true, /*bForce=*/true);
			GEditor->EndTransaction();

			return FMCPToolResult::Success(
				FString::Printf(
					TEXT("Added %d foliage instance(s) of '%s' within radius %.0f around (%.1f, %.1f, %.1f)"),
					Added,
					*Mesh->GetName(),
					Radius,
					CenterX, CenterY, CenterZ));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// erase_foliage - Remove foliage instances in a radius
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("x"),
			TEXT("Center X position in world space"),
			/*bRequired=*/true);
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("y"),
			TEXT("Center Y position in world space"),
			/*bRequired=*/true);
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("z"),
			TEXT("Center Z position in world space"),
			/*bRequired=*/true);
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("radius"),
			TEXT("Radius in cm within which instances are removed"),
			/*bRequired=*/true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("mesh_path"),
			TEXT("Optional: limit erasure to foliage using this mesh. Leave empty to erase all types."));

		FMCPToolDefinition Def;
		Def.Name = TEXT("erase_foliage");
		Def.Description = TEXT(
			"Remove foliage instances within a spherical radius around a world-space center. "
			"If mesh_path is provided only instances of that foliage type are erased; "
			"otherwise all foliage types are affected.");
		Def.InputSchema = Schema;
		Def.bDestructiveHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World)
			{
				return FMCPToolResult::Error(TEXT("No editor world available"));
			}

			if (!Args->HasField(TEXT("x")) || !Args->HasField(TEXT("y")) ||
			    !Args->HasField(TEXT("z")) || !Args->HasField(TEXT("radius")))
			{
				return FMCPToolResult::Error(TEXT("x, y, z, and radius are required"));
			}

			const float CenterX  = (float)Args->GetNumberField(TEXT("x"));
			const float CenterY  = (float)Args->GetNumberField(TEXT("y"));
			const float CenterZ  = (float)Args->GetNumberField(TEXT("z"));
			const float Radius   = (float)Args->GetNumberField(TEXT("radius"));
			const float RadiusSq = Radius * Radius;

			FString FilterMeshPath;
			Args->TryGetStringField(TEXT("mesh_path"), FilterMeshPath);

			UStaticMesh* FilterMesh = nullptr;
			if (!FilterMeshPath.IsEmpty())
			{
				FilterMesh = LoadObject<UStaticMesh>(nullptr, *FilterMeshPath);
				if (!IsValid(FilterMesh))
				{
					return FMCPToolResult::Error(
						FString::Printf(TEXT("Static mesh not found: %s"), *FilterMeshPath));
				}
			}

			AInstancedFoliageActor* FoliageActor =
				AInstancedFoliageActor::GetInstancedFoliageActorForCurrentLevel(
					World, /*bCreateIfNone=*/false);

			if (!FoliageActor)
			{
				return FMCPToolResult::Success(
					TEXT("No InstancedFoliageActor found in this level; nothing to erase"));
			}

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Erase Foliage")));
			FoliageActor->Modify();

			const FVector Center(CenterX, CenterY, CenterZ);
			int32 TotalRemoved = 0;

			// Use ForEachFoliageInfo to iterate without touching private FoliageInfos
			FoliageActor->ForEachFoliageInfo([&](UFoliageType* FoliageType, FFoliageInfo& FoliageInfo) -> bool
			{
				if (!IsValid(FoliageType))
				{
					return true; // continue
				}

				// Apply optional per-mesh filter
				if (FilterMesh)
				{
					UFoliageType_InstancedStaticMesh* ISMType =
						Cast<UFoliageType_InstancedStaticMesh>(FoliageType);
					if (!ISMType || ISMType->GetStaticMesh() != FilterMesh)
					{
						return true; // continue to next type
					}
				}

				// Collect instance indices inside the radius.
				// FFoliageInfo::Instances is guarded by WITH_EDITORONLY_DATA and is
				// accessible inside a WITH_EDITOR context (which editor plugins are).
				TArray<int32> ToRemove;
				for (int32 Idx = 0; Idx < FoliageInfo.Instances.Num(); ++Idx)
				{
					const FVector InstanceLoc = FoliageInfo.Instances[Idx].Location;
					if (FVector::DistSquared(InstanceLoc, Center) <= RadiusSq)
					{
						ToRemove.Add(Idx);
					}
				}

				if (ToRemove.Num() > 0)
				{
					// Indices must be sorted descending so earlier removals don't
					// shift indices of later ones
					ToRemove.Sort([](int32 A, int32 B) { return A > B; });
					FoliageInfo.RemoveInstances(ToRemove, /*bRebuildTree=*/true);
					TotalRemoved += ToRemove.Num();
				}

				return true; // continue iterating
			});

			GEditor->EndTransaction();

			if (FilterMesh)
			{
				return FMCPToolResult::Success(
					FString::Printf(
						TEXT("Removed %d instance(s) of '%s' within radius %.0f around (%.1f, %.1f, %.1f)"),
						TotalRemoved,
						*FilterMesh->GetName(),
						Radius,
						CenterX, CenterY, CenterZ));
			}

			return FMCPToolResult::Success(
				FString::Printf(
					TEXT("Removed %d foliage instance(s) (all types) within radius %.0f around (%.1f, %.1f, %.1f)"),
					TotalRemoved,
					Radius,
					CenterX, CenterY, CenterZ));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// get_foliage_stats - Get instance counts per foliage type
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		// No required parameters

		FMCPToolDefinition Def;
		Def.Name = TEXT("get_foliage_stats");
		Def.Description = TEXT(
			"Return a summary of all foliage types registered in the current level, "
			"including the static mesh name and total instance count for each type. "
			"Returns an empty list when no InstancedFoliageActor exists in the level.");
		Def.InputSchema = Schema;
		Def.bReadOnlyHint = true;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World)
			{
				return FMCPToolResult::Error(TEXT("No editor world available"));
			}

			AInstancedFoliageActor* FoliageActor =
				AInstancedFoliageActor::GetInstancedFoliageActorForCurrentLevel(
					World, /*bCreateIfNone=*/false);

			if (!FoliageActor)
			{
				return FMCPToolResult::Success(
					TEXT("No InstancedFoliageActor found in this level. "
					     "Foliage stats: 0 types, 0 instances."));
			}

			TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
			TArray<TSharedPtr<FJsonValue>> TypesArray;

			int32 TotalTypes     = 0;
			int32 TotalInstances = 0;

			// GetFoliageInfos() returns a const TMap<UFoliageType*, TUniqueObj<FFoliageInfo>>&
			for (const auto& Pair : FoliageActor->GetFoliageInfos())
			{
				UFoliageType* FoliageType = Pair.Key;
				if (!IsValid(FoliageType))
				{
					continue;
				}

				const FFoliageInfo& FoliageInfo = Pair.Value.Get();
				// FFoliageInfo::Instances is WITH_EDITORONLY_DATA; always available
				// in an editor module
				const int32 InstanceCount = FoliageInfo.Instances.Num();

				TSharedPtr<FJsonObject> TypeObj = MakeShared<FJsonObject>();
				TypeObj->SetStringField(TEXT("foliage_type"),  FoliageType->GetName());
				TypeObj->SetStringField(TEXT("foliage_class"), FoliageType->GetClass()->GetName());

				const UFoliageType_InstancedStaticMesh* ISMType =
					Cast<UFoliageType_InstancedStaticMesh>(FoliageType);
				if (ISMType && IsValid(ISMType->GetStaticMesh()))
				{
					TypeObj->SetStringField(TEXT("mesh_name"),
						ISMType->GetStaticMesh()->GetName());
					TypeObj->SetStringField(TEXT("mesh_path"),
						ISMType->GetStaticMesh()->GetPathName());
				}
				else
				{
					TypeObj->SetStringField(TEXT("mesh_name"), TEXT("(none)"));
					TypeObj->SetStringField(TEXT("mesh_path"), TEXT(""));
				}

				TypeObj->SetNumberField(TEXT("instance_count"), InstanceCount);

				TypesArray.Add(MakeShared<FJsonValueObject>(TypeObj));
				TotalTypes++;
				TotalInstances += InstanceCount;
			}

			ResultObj->SetArrayField(TEXT("foliage_types"),    TypesArray);
			ResultObj->SetNumberField(TEXT("total_types"),     TotalTypes);
			ResultObj->SetNumberField(TEXT("total_instances"), TotalInstances);

			return FMCPToolResult::Success(JsonToString(ResultObj));
		});
		Registry.RegisterTool(Def);
	}
}

} // namespace MCPFoliageTools
