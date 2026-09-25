// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "MCPToolRegistry.h"
#include "MCPValidate.h"
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

namespace MCPSpatialTools::Placement
{

using namespace MCPSpatialTools::Common;

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// place_actor_on_ground - Drop an actor to sit on the surface below
	// ================================================================
	MCP_TOOL(Registry, "place_actor_on_ground")
		.Description(TEXT("Move an actor down (or up) to sit on the ground/surface below it. Traces straight down to find the surface, then positions the actor so its bottom sits on that surface."))
		.Idempotent()
		.StringArg(TEXT("actor_name"), TEXT("Label of the actor to place on ground"), true)
		.NumberArg(TEXT("offset_z"), TEXT("Extra height above the ground surface (default: 0)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			FString ActorName;
			if (!Args->TryGetStringField(TEXT("actor_name"), ActorName))
				return FMCPToolResult::Error(TEXT("actor_name is required"));

			AActor* Actor = FindActorByLabel(World, ActorName);
			if (!Actor) return FMCPToolResult::Error(FString::Printf(TEXT("Actor not found: %s"), *ActorName));

			double OffsetZ = 0.0;
			if (Args->HasField(TEXT("offset_z"))) OffsetZ = Args->GetNumberField(TEXT("offset_z"));

			// Get actor bounds
			FVector Origin, BoxExtent;
			Actor->GetActorBounds(false, Origin, BoxExtent);

			// Trace from actor center straight down
			FVector TraceStart = Origin;
			FVector TraceEnd = Origin - FVector(0, 0, 100000.0); // 1km down

			FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MCPPlaceOnGround), true);
			QueryParams.AddIgnoredActor(Actor);

			FHitResult HitResult;
			bool bHit = World->LineTraceSingleByChannel(HitResult, TraceStart, TraceEnd, ECC_Visibility, QueryParams);

			if (!bHit)
			{
				return FMCPToolResult::Error(TEXT("No ground surface found below the actor"));
			}

			// Calculate new position: hit point + half bounds height + offset
			FVector ActorLocation = Actor->GetActorLocation();
			double ActorBottomOffset = Origin.Z - BoxExtent.Z - ActorLocation.Z;
			FVector NewLocation = ActorLocation;
			NewLocation.Z = HitResult.Location.Z - ActorBottomOffset + OffsetZ;

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Place Actor On Ground")));
			Actor->Modify();
			Actor->SetActorLocation(NewLocation);
			GEditor->EndTransaction();

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("actor"), ActorName);
			Result->SetObjectField(TEXT("new_position"), MakeVectorJson(NewLocation));
			Result->SetObjectField(TEXT("ground_hit_point"), MakeVectorJson(HitResult.Location));
			Result->SetObjectField(TEXT("ground_normal"), MakeVectorJson(HitResult.Normal));

			if (HitResult.GetActor())
			{
				Result->SetStringField(TEXT("surface_actor"), HitResult.GetActor()->GetActorLabel());
			}
			if (HitResult.PhysMaterial.IsValid())
			{
				Result->SetStringField(TEXT("surface_material"), HitResult.PhysMaterial->GetName());
			}

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});

	// ================================================================
	// get_spatial_context - High-level spatial analysis of the scene
	// ================================================================
	MCP_TOOL(Registry, "get_spatial_context")
		.Description(TEXT("High-level spatial analysis of the current scene. Returns scene bounds, actor density, ground level, nearest actors, density map by quadrant, and empty spaces. Gives the AI a bird's-eye understanding of the scene layout."))
		.ReadOnly()
		.Idempotent()
		.NumberArg(TEXT("center_x"), TEXT("Center X of analysis region (default: scene center)"))
		.NumberArg(TEXT("center_y"), TEXT("Center Y of analysis region"))
		.NumberArg(TEXT("center_z"), TEXT("Center Z of analysis region"))
		.NumberArg(TEXT("radius"), TEXT("Radius of analysis region in units (default: 5000)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			double Radius = 5000.0;
			if (Args->HasField(TEXT("radius"))) Radius = Args->GetNumberField(TEXT("radius"));

			// Gather all actors and compute scene bounds
			FVector SceneMin(MAX_dbl, MAX_dbl, MAX_dbl);
			FVector SceneMax(-MAX_dbl, -MAX_dbl, -MAX_dbl);
			TArray<AActor*> AllActors;
			double SmallestSize = MAX_dbl;
			double LargestSize = 0.0;

			for (TActorIterator<AActor> It(World); It; ++It)
			{
				AActor* Actor = *It;
				if (!IsValid(Actor)) continue;
				// Skip transient / editor-only actors
				if (Actor->IsA(ABrush::StaticClass())) continue;
				if (Actor->IsHidden()) continue;

				FVector Origin, BoxExtent;
				Actor->GetActorBounds(false, Origin, BoxExtent);

				// Skip actors with zero bounds (world settings, etc)
				if (BoxExtent.IsNearlyZero()) continue;

				AllActors.Add(Actor);

				FVector ActorMin = Origin - BoxExtent;
				FVector ActorMax = Origin + BoxExtent;
				SceneMin = SceneMin.ComponentMin(ActorMin);
				SceneMax = SceneMax.ComponentMax(ActorMax);

				double Size = BoxExtent.Size();
				SmallestSize = FMath::Min(SmallestSize, Size);
				LargestSize = FMath::Max(LargestSize, Size);
			}

			if (AllActors.Num() == 0)
			{
				return FMCPToolResult::Success(TEXT("{\"actor_count\": 0, \"message\": \"No actors in scene\"}"));
			}

			FVector SceneCenter = (SceneMin + SceneMax) * 0.5;
			FVector SceneSize = SceneMax - SceneMin;

			// Use provided center or scene center
			FVector AnalysisCenter = SceneCenter;
			if (Args->HasField(TEXT("center_x")))
			{
				AnalysisCenter.X = Args->GetNumberField(TEXT("center_x"));
				AnalysisCenter.Y = Args->HasField(TEXT("center_y")) ? Args->GetNumberField(TEXT("center_y")) : SceneCenter.Y;
				AnalysisCenter.Z = Args->HasField(TEXT("center_z")) ? Args->GetNumberField(TEXT("center_z")) : SceneCenter.Z;
			}

			// Filter actors within radius and find nearest
			struct FActorDist
			{
				AActor* Actor;
				double Distance;
			};
			TArray<FActorDist> ActorsInRadius;

			for (AActor* Actor : AllActors)
			{
				double Dist = FVector::Dist(Actor->GetActorLocation(), AnalysisCenter);
				if (Dist <= Radius)
				{
					ActorsInRadius.Add({Actor, Dist});
				}
			}

			// Sort by distance
			ActorsInRadius.Sort([](const FActorDist& A, const FActorDist& B) { return A.Distance < B.Distance; });

			// Nearest actors (top 10)
			TArray<TSharedPtr<FJsonValue>> NearestArray;
			int32 MaxNearest = FMath::Min(ActorsInRadius.Num(), 10);
			for (int32 i = 0; i < MaxNearest; i++)
			{
				AActor* Actor = ActorsInRadius[i].Actor;
				FVector Origin, BoxExtent;
				Actor->GetActorBounds(false, Origin, BoxExtent);

				TSharedPtr<FJsonObject> NearObj = MakeShared<FJsonObject>();
				NearObj->SetStringField(TEXT("name"), Actor->GetActorLabel());
				NearObj->SetStringField(TEXT("class"), Actor->GetClass()->GetName());
				NearObj->SetNumberField(TEXT("distance"), ActorsInRadius[i].Distance);
				NearObj->SetObjectField(TEXT("position"), MakeVectorJson(Actor->GetActorLocation()));
				NearObj->SetObjectField(TEXT("bounds_size"), MakeVectorJson(BoxExtent * 2.0));
				NearestArray.Add(MakeShared<FJsonValueObject>(NearObj));
			}

			// Density map (quadrants based on X/Y relative to center)
			int32 CountNW = 0, CountNE = 0, CountSW = 0, CountSE = 0;
			for (const FActorDist& AD : ActorsInRadius)
			{
				FVector Loc = AD.Actor->GetActorLocation();
				bool bEast = Loc.X >= AnalysisCenter.X;
				bool bNorth = Loc.Y >= AnalysisCenter.Y;
				if (bNorth && bEast) CountNE++;
				else if (bNorth && !bEast) CountNW++;
				else if (!bNorth && bEast) CountSE++;
				else CountSW++;
			}

			TSharedPtr<FJsonObject> DensityMap = MakeShared<FJsonObject>();
			DensityMap->SetNumberField(TEXT("NE_count"), CountNE);
			DensityMap->SetNumberField(TEXT("NW_count"), CountNW);
			DensityMap->SetNumberField(TEXT("SE_count"), CountSE);
			DensityMap->SetNumberField(TEXT("SW_count"), CountSW);
			DensityMap->SetStringField(TEXT("densest_quadrant"),
				(CountNE >= CountNW && CountNE >= CountSE && CountNE >= CountSW) ? TEXT("NE") :
				(CountNW >= CountNE && CountNW >= CountSE && CountNW >= CountSW) ? TEXT("NW") :
				(CountSE >= CountNE && CountSE >= CountNW && CountSE >= CountSW) ? TEXT("SE") : TEXT("SW"));
			DensityMap->SetStringField(TEXT("emptiest_quadrant"),
				(CountNE <= CountNW && CountNE <= CountSE && CountNE <= CountSW) ? TEXT("NE") :
				(CountNW <= CountNE && CountNW <= CountSE && CountNW <= CountSW) ? TEXT("NW") :
				(CountSE <= CountNE && CountSE <= CountNW && CountSE <= CountSW) ? TEXT("SE") : TEXT("SW"));

			// Ground level estimation: trace down from center at several points
			double GroundSum = 0.0;
			int32 GroundHits = 0;
			FCollisionQueryParams GroundParams(SCENE_QUERY_STAT(MCPGroundTrace), true);
			TArray<FVector> SamplePoints = {
				AnalysisCenter,
				AnalysisCenter + FVector(Radius * 0.5, 0, 0),
				AnalysisCenter + FVector(-Radius * 0.5, 0, 0),
				AnalysisCenter + FVector(0, Radius * 0.5, 0),
				AnalysisCenter + FVector(0, -Radius * 0.5, 0)
			};

			for (const FVector& Pt : SamplePoints)
			{
				FVector TraceStart(Pt.X, Pt.Y, SceneMax.Z + 1000.0);
				FVector TraceEnd(Pt.X, Pt.Y, SceneMin.Z - 1000.0);
				FHitResult Hit;
				if (World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_Visibility, GroundParams))
				{
					GroundSum += Hit.Location.Z;
					GroundHits++;
				}
			}

			// Empty spaces analysis: check grid cells for no actors
			TArray<TSharedPtr<FJsonValue>> EmptySpaces;
			double CellSize = Radius * 0.5;
			for (double dx = -1; dx <= 1; dx += 1.0)
			{
				for (double dy = -1; dy <= 1; dy += 1.0)
				{
					FVector CellCenter = AnalysisCenter + FVector(dx * CellSize, dy * CellSize, 0);
					bool bOccupied = false;
					for (const FActorDist& AD : ActorsInRadius)
					{
						FVector Loc = AD.Actor->GetActorLocation();
						if (FMath::Abs(Loc.X - CellCenter.X) < CellSize * 0.5 &&
							FMath::Abs(Loc.Y - CellCenter.Y) < CellSize * 0.5)
						{
							bOccupied = true;
							break;
						}
					}
					if (!bOccupied)
					{
						TSharedPtr<FJsonObject> SpaceObj = MakeShared<FJsonObject>();
						SpaceObj->SetObjectField(TEXT("center"), MakeVectorJson(CellCenter));
						SpaceObj->SetNumberField(TEXT("approximate_size"), CellSize);
						EmptySpaces.Add(MakeShared<FJsonValueObject>(SpaceObj));
					}
				}
			}

			// Build comprehensive result
			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

			// Scene bounds
			TSharedPtr<FJsonObject> BoundsObj = MakeShared<FJsonObject>();
			BoundsObj->SetObjectField(TEXT("min"), MakeVectorJson(SceneMin));
			BoundsObj->SetObjectField(TEXT("max"), MakeVectorJson(SceneMax));
			BoundsObj->SetObjectField(TEXT("size"), MakeVectorJson(SceneSize));
			BoundsObj->SetObjectField(TEXT("center"), MakeVectorJson(SceneCenter));
			Result->SetObjectField(TEXT("scene_bounds"), BoundsObj);

			Result->SetNumberField(TEXT("total_actor_count"), AllActors.Num());
			Result->SetNumberField(TEXT("actors_in_radius"), ActorsInRadius.Num());
			Result->SetObjectField(TEXT("analysis_center"), MakeVectorJson(AnalysisCenter));
			Result->SetNumberField(TEXT("analysis_radius"), Radius);

			if (GroundHits > 0)
			{
				Result->SetNumberField(TEXT("ground_level_z"), GroundSum / GroundHits);
			}

			Result->SetArrayField(TEXT("nearest_actors"), NearestArray);
			Result->SetObjectField(TEXT("density_map"), DensityMap);
			Result->SetArrayField(TEXT("empty_spaces"), EmptySpaces);

			// Bounding summary
			TSharedPtr<FJsonObject> BoundingSummary = MakeShared<FJsonObject>();
			BoundingSummary->SetNumberField(TEXT("smallest_actor_extent"), SmallestSize);
			BoundingSummary->SetNumberField(TEXT("largest_actor_extent"), LargestSize);

			// Calculate average spacing between nearest actor pairs
			if (ActorsInRadius.Num() >= 2)
			{
				double SpacingSum = 0.0;
				int32 SpacingCount = 0;
				int32 MaxPairs = FMath::Min(ActorsInRadius.Num(), 20);
				for (int32 i = 0; i < MaxPairs; i++)
				{
					double MinDist = MAX_dbl;
					for (int32 j = 0; j < ActorsInRadius.Num(); j++)
					{
						if (i == j) continue;
						double D = FVector::Dist(ActorsInRadius[i].Actor->GetActorLocation(),
							ActorsInRadius[j].Actor->GetActorLocation());
						MinDist = FMath::Min(MinDist, D);
					}
					if (MinDist < MAX_dbl)
					{
						SpacingSum += MinDist;
						SpacingCount++;
					}
				}
				if (SpacingCount > 0)
				{
					BoundingSummary->SetNumberField(TEXT("average_nearest_spacing"), SpacingSum / SpacingCount);
				}
			}

			Result->SetObjectField(TEXT("bounding_summary"), BoundingSummary);

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});

	// ================================================================
	// find_placement_position - Find a clear position to place something
	// ================================================================
	MCP_TOOL(Registry, "find_placement_position")
		.Description(TEXT("AI-friendly tool: find a good position to place something. Tries the desired position, checks overlap, and spirals outward to find clear space if blocked. Optionally snaps to ground."))
		.ReadOnly()
		.Idempotent()
		.NumberArg(TEXT("near_x"), TEXT("Desired X position"), true)
		.NumberArg(TEXT("near_y"), TEXT("Desired Y position"), true)
		.NumberArg(TEXT("near_z"), TEXT("Desired Z position"), true)
		.NumberArg(TEXT("required_size_x"), TEXT("Required size X (full width of object to place)"), true)
		.NumberArg(TEXT("required_size_y"), TEXT("Required size Y (full depth)"), true)
		.NumberArg(TEXT("required_size_z"), TEXT("Required size Z (full height)"), true)
		.BoolArg(TEXT("on_ground"), TEXT("Trace to ground and place on surface (default: true)"))
		.NumberArg(TEXT("min_distance_from_actors"), TEXT("Minimum distance from any existing actor (default: 0)"))
		.EnumArg(TEXT("prefer_direction"), TEXT("Preferred direction to search if desired position is blocked"),
			{ TEXT("any"), TEXT("north"), TEXT("south"), TEXT("east"), TEXT("west") })
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			// v4 (matrix-found): missing args silently searched around the origin
			// with a zero-size box.
			double NX, NY, NZ, RX, RY, RZ;
			BAIL_IF_INVALID(FMCPValidate::RequiredNumber(Args, TEXT("near_x"), NX));
			BAIL_IF_INVALID(FMCPValidate::RequiredNumber(Args, TEXT("near_y"), NY));
			BAIL_IF_INVALID(FMCPValidate::RequiredNumber(Args, TEXT("near_z"), NZ));
			BAIL_IF_INVALID(FMCPValidate::RequiredNumber(Args, TEXT("required_size_x"), RX));
			BAIL_IF_INVALID(FMCPValidate::RequiredNumber(Args, TEXT("required_size_y"), RY));
			BAIL_IF_INVALID(FMCPValidate::RequiredNumber(Args, TEXT("required_size_z"), RZ));

			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			FVector DesiredPos(NX, NY, NZ);
			FVector RequiredSize(RX, RY, RZ);

			FVector HalfExtent = RequiredSize * 0.5;

			bool bOnGround = true;
			if (Args->HasField(TEXT("on_ground"))) Args->TryGetBoolField(TEXT("on_ground"), bOnGround);

			double MinDist = 0.0;
			if (Args->HasField(TEXT("min_distance_from_actors"))) MinDist = Args->GetNumberField(TEXT("min_distance_from_actors"));

			FString PreferDir = TEXT("any");
			Args->TryGetStringField(TEXT("prefer_direction"), PreferDir);

			// Lambda to test if a position is clear
			auto TestPosition = [&](const FVector& Pos) -> bool
			{
				FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MCPFindPlacement), true);
				FCollisionShape Shape = FCollisionShape::MakeBox(HalfExtent + FVector(MinDist));
				TArray<FOverlapResult> Overlaps;
				return !World->OverlapMultiByChannel(Overlaps, Pos, FQuat::Identity, ECC_WorldStatic, Shape, QueryParams) || Overlaps.Num() == 0;
			};

			FVector FinalPos = DesiredPos;
			bool bExact = true;
			FString AdjustedReason;

			if (!TestPosition(DesiredPos))
			{
				bExact = false;
				bool bFound = false;

				// Determine search directions based on preference
				TArray<FVector> SearchDirs;
				if (PreferDir == TEXT("north"))
				{
					SearchDirs.Add(FVector(0, 1, 0)); // +Y first
					SearchDirs.Add(FVector(1, 0, 0));
					SearchDirs.Add(FVector(0, -1, 0));
					SearchDirs.Add(FVector(-1, 0, 0));
				}
				else if (PreferDir == TEXT("south"))
				{
					SearchDirs.Add(FVector(0, -1, 0));
					SearchDirs.Add(FVector(1, 0, 0));
					SearchDirs.Add(FVector(0, 1, 0));
					SearchDirs.Add(FVector(-1, 0, 0));
				}
				else if (PreferDir == TEXT("east"))
				{
					SearchDirs.Add(FVector(1, 0, 0)); // +X first
					SearchDirs.Add(FVector(0, 1, 0));
					SearchDirs.Add(FVector(-1, 0, 0));
					SearchDirs.Add(FVector(0, -1, 0));
				}
				else if (PreferDir == TEXT("west"))
				{
					SearchDirs.Add(FVector(-1, 0, 0));
					SearchDirs.Add(FVector(0, 1, 0));
					SearchDirs.Add(FVector(1, 0, 0));
					SearchDirs.Add(FVector(0, -1, 0));
				}
				else // "any" - spiral outward
				{
					SearchDirs.Add(FVector(1, 0, 0));
					SearchDirs.Add(FVector(0, 1, 0));
					SearchDirs.Add(FVector(-1, 0, 0));
					SearchDirs.Add(FVector(0, -1, 0));
					SearchDirs.Add(FVector(1, 1, 0).GetSafeNormal());
					SearchDirs.Add(FVector(-1, 1, 0).GetSafeNormal());
					SearchDirs.Add(FVector(1, -1, 0).GetSafeNormal());
					SearchDirs.Add(FVector(-1, -1, 0).GetSafeNormal());
				}

				// Search at increasing distances
				double StepSize = FMath::Max(RequiredSize.GetMax(), 100.0);
				for (int32 Ring = 1; Ring <= 20 && !bFound; Ring++)
				{
					double Dist = StepSize * Ring;
					for (const FVector& Dir : SearchDirs)
					{
						FVector TestPos = DesiredPos + Dir * Dist;
						if (TestPosition(TestPos))
						{
							FinalPos = TestPos;
							bFound = true;
							AdjustedReason = FString::Printf(TEXT("Moved %.0f units %s to avoid overlap"),
								Dist,
								Dir.X > 0.5 ? TEXT("east") :
								Dir.X < -0.5 ? TEXT("west") :
								Dir.Y > 0.5 ? TEXT("north") :
								TEXT("south"));
							break;
						}
					}
				}

				if (!bFound)
				{
					AdjustedReason = TEXT("Could not find clear space within search range, using desired position");
					FinalPos = DesiredPos;
				}
			}

			// Trace to ground if requested
			if (bOnGround)
			{
				FCollisionQueryParams GroundParams(SCENE_QUERY_STAT(MCPPlacementGround), true);
				FVector TraceStart(FinalPos.X, FinalPos.Y, FinalPos.Z + 10000.0);
				FVector TraceEnd(FinalPos.X, FinalPos.Y, FinalPos.Z - 100000.0);
				FHitResult Hit;
				if (World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_Visibility, GroundParams))
				{
					FinalPos.Z = Hit.Location.Z + HalfExtent.Z;
				}
			}

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetObjectField(TEXT("position"), MakeVectorJson(FinalPos));
			Result->SetBoolField(TEXT("is_exact"), bExact);
			Result->SetObjectField(TEXT("desired_position"), MakeVectorJson(DesiredPos));
			Result->SetObjectField(TEXT("required_size"), MakeVectorJson(RequiredSize));

			if (!AdjustedReason.IsEmpty())
			{
				Result->SetStringField(TEXT("adjusted_reason"), AdjustedReason);
			}

			Result->SetNumberField(TEXT("distance_from_desired"), FVector::Dist(DesiredPos, FinalPos));

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});

} // void RegisterAll

} // namespace MCPSpatialTools::Placement
