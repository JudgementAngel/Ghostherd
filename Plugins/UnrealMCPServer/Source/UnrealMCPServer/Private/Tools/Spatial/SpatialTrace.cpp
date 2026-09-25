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

namespace MCPSpatialTools::Trace
{

using namespace MCPSpatialTools::Common;

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// line_trace - Cast a ray and report what it hits
	// ================================================================
	MCP_TOOL(Registry, "line_trace")
		.Description(TEXT("Cast a ray from A to B and report what it hits. Critical for finding ground level, checking line of sight, and placing on surfaces."))
		.ReadOnly()
		.Idempotent()
		.NumberArg(TEXT("start_x"), TEXT("Start X position"), true)
		.NumberArg(TEXT("start_y"), TEXT("Start Y position"), true)
		.NumberArg(TEXT("start_z"), TEXT("Start Z position"), true)
		.NumberArg(TEXT("end_x"), TEXT("End X position"), true)
		.NumberArg(TEXT("end_y"), TEXT("End Y position"), true)
		.NumberArg(TEXT("end_z"), TEXT("End Z position"), true)
		.StringArg(TEXT("trace_channel"), TEXT("Collision channel (default: 'Visibility'). Options: Visibility, Camera, WorldStatic, WorldDynamic, Pawn, PhysicsBody"))
		.StringArrayArg(TEXT("ignore_actors"), TEXT("Array of actor labels to ignore during the trace"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			// v4 (matrix-found): missing coords silently traced (0,0,0)->(0,0,0).
			double SX, SY, SZ, EX, EY, EZ;
			BAIL_IF_INVALID(FMCPValidate::RequiredNumber(Args, TEXT("start_x"), SX));
			BAIL_IF_INVALID(FMCPValidate::RequiredNumber(Args, TEXT("start_y"), SY));
			BAIL_IF_INVALID(FMCPValidate::RequiredNumber(Args, TEXT("start_z"), SZ));
			BAIL_IF_INVALID(FMCPValidate::RequiredNumber(Args, TEXT("end_x"), EX));
			BAIL_IF_INVALID(FMCPValidate::RequiredNumber(Args, TEXT("end_y"), EY));
			BAIL_IF_INVALID(FMCPValidate::RequiredNumber(Args, TEXT("end_z"), EZ));

			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			FVector Start(SX, SY, SZ);
			FVector End(EX, EY, EZ);

			FString ChannelStr = TEXT("Visibility");
			Args->TryGetStringField(TEXT("trace_channel"), ChannelStr);
			ECollisionChannel Channel = ParseTraceChannel(ChannelStr);

			FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MCPLineTrace), true);

			// Add actors to ignore
			if (Args->HasField(TEXT("ignore_actors")))
			{
				TArray<TSharedPtr<FJsonValue>> IgnoreNames = Args->GetArrayField(TEXT("ignore_actors"));
				for (const auto& Val : IgnoreNames)
				{
					FString Name;
					if (Val->TryGetString(Name))
					{
						AActor* IgnoreActor = FindActorByLabel(World, Name);
						if (IgnoreActor)
						{
							QueryParams.AddIgnoredActor(IgnoreActor);
						}
					}
				}
			}

			FHitResult HitResult;
			bool bHit = World->LineTraceSingleByChannel(HitResult, Start, End, Channel, QueryParams);

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetBoolField(TEXT("hit"), bHit);

			if (bHit)
			{
				Result->SetObjectField(TEXT("hit_location"), MakeVectorJson(HitResult.Location));
				Result->SetObjectField(TEXT("hit_normal"), MakeVectorJson(HitResult.Normal));
				Result->SetNumberField(TEXT("hit_distance"), HitResult.Distance);

				if (HitResult.GetActor())
				{
					Result->SetStringField(TEXT("hit_actor"), HitResult.GetActor()->GetActorLabel());
					Result->SetStringField(TEXT("hit_actor_class"), HitResult.GetActor()->GetClass()->GetName());
				}
				if (HitResult.GetComponent())
				{
					Result->SetStringField(TEXT("hit_component"), HitResult.GetComponent()->GetName());
				}

				// Try to get physical material name
				if (HitResult.PhysMaterial.IsValid())
				{
					Result->SetStringField(TEXT("hit_material"), HitResult.PhysMaterial->GetName());
				}
			}

			Result->SetObjectField(TEXT("trace_start"), MakeVectorJson(Start));
			Result->SetObjectField(TEXT("trace_end"), MakeVectorJson(End));
			Result->SetNumberField(TEXT("trace_length"), FVector::Dist(Start, End));

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});

	// ================================================================
	// overlap_test - Check if something overlaps at a position
	// ================================================================
	MCP_TOOL(Registry, "overlap_test")
		.Description(TEXT("Check if an actor overlaps with anything at its current position, or test a hypothetical box at a position. Critical for collision-free placement."))
		.ReadOnly()
		.Idempotent()
		.StringArg(TEXT("actor_name"), TEXT("Actor to test overlap for (uses actor's current bounds). Mutually exclusive with test_x/y/z + test_extent_x/y/z."))
		.NumberArg(TEXT("test_x"), TEXT("X position of test box center (use with test_extent)"))
		.NumberArg(TEXT("test_y"), TEXT("Y position of test box center"))
		.NumberArg(TEXT("test_z"), TEXT("Z position of test box center"))
		.NumberArg(TEXT("test_extent_x"), TEXT("Half-extent X of test box (default: 50)"))
		.NumberArg(TEXT("test_extent_y"), TEXT("Half-extent Y of test box (default: 50)"))
		.NumberArg(TEXT("test_extent_z"), TEXT("Half-extent Z of test box (default: 50)"))
		.StringArrayArg(TEXT("ignore_actors"), TEXT("Array of actor labels to ignore"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			FVector TestCenter;
			FVector TestExtent(50.0, 50.0, 50.0);
			AActor* TestActor = nullptr;

			FString ActorName;
			if (Args->TryGetStringField(TEXT("actor_name"), ActorName))
			{
				TestActor = FindActorByLabel(World, ActorName);
				if (!TestActor) return FMCPToolResult::Error(FString::Printf(TEXT("Actor not found: %s"), *ActorName));

				FVector Origin, BoxExtent;
				TestActor->GetActorBounds(false, Origin, BoxExtent);
				TestCenter = Origin;
				TestExtent = BoxExtent;
			}
			else if (Args->HasField(TEXT("test_x")))
			{
				TestCenter.X = Args->GetNumberField(TEXT("test_x"));
				TestCenter.Y = Args->HasField(TEXT("test_y")) ? Args->GetNumberField(TEXT("test_y")) : 0.0;
				TestCenter.Z = Args->HasField(TEXT("test_z")) ? Args->GetNumberField(TEXT("test_z")) : 0.0;

				if (Args->HasField(TEXT("test_extent_x"))) TestExtent.X = Args->GetNumberField(TEXT("test_extent_x"));
				if (Args->HasField(TEXT("test_extent_y"))) TestExtent.Y = Args->GetNumberField(TEXT("test_extent_y"));
				if (Args->HasField(TEXT("test_extent_z"))) TestExtent.Z = Args->GetNumberField(TEXT("test_extent_z"));
			}
			else
			{
				return FMCPToolResult::Error(TEXT("Provide either actor_name or test_x/y/z position"));
			}

			FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MCPOverlapTest), true);
			if (TestActor)
			{
				QueryParams.AddIgnoredActor(TestActor);
			}

			// Add actors to ignore
			if (Args->HasField(TEXT("ignore_actors")))
			{
				TArray<TSharedPtr<FJsonValue>> IgnoreNames = Args->GetArrayField(TEXT("ignore_actors"));
				for (const auto& Val : IgnoreNames)
				{
					FString Name;
					if (Val->TryGetString(Name))
					{
						AActor* IgnoreActor = FindActorByLabel(World, Name);
						if (IgnoreActor) QueryParams.AddIgnoredActor(IgnoreActor);
					}
				}
			}

			FCollisionShape Shape = FCollisionShape::MakeBox(TestExtent);
			TArray<FOverlapResult> Overlaps;
			bool bHasOverlap = World->OverlapMultiByChannel(Overlaps, TestCenter, FQuat::Identity, ECC_WorldStatic, Shape, QueryParams);

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetBoolField(TEXT("has_overlap"), bHasOverlap);
			Result->SetObjectField(TEXT("test_center"), MakeVectorJson(TestCenter));
			Result->SetObjectField(TEXT("test_extent"), MakeVectorJson(TestExtent));

			TArray<TSharedPtr<FJsonValue>> OverlapArray;
			TSet<AActor*> SeenActors;
			for (const FOverlapResult& Overlap : Overlaps)
			{
				AActor* OverlapActor = Overlap.GetActor();
				if (!OverlapActor || SeenActors.Contains(OverlapActor)) continue;
				SeenActors.Add(OverlapActor);

				TSharedPtr<FJsonObject> OverlapObj = MakeShared<FJsonObject>();
				OverlapObj->SetStringField(TEXT("name"), OverlapActor->GetActorLabel());
				OverlapObj->SetStringField(TEXT("class"), OverlapActor->GetClass()->GetName());

				// Compute approximate overlap distance
				FVector OtherOrigin, OtherExtent;
				OverlapActor->GetActorBounds(false, OtherOrigin, OtherExtent);
				double Dist = FVector::Dist(TestCenter, OtherOrigin);
				OverlapObj->SetNumberField(TEXT("distance_to_center"), Dist);

				OverlapArray.Add(MakeShared<FJsonValueObject>(OverlapObj));
			}
			Result->SetArrayField(TEXT("overlapping_actors"), OverlapArray);
			Result->SetNumberField(TEXT("overlap_count"), OverlapArray.Num());

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});

	// ================================================================
	// measure_distance - Measure distance between two points or actors
	// ================================================================
	MCP_TOOL(Registry, "measure_distance")
		.Description(TEXT("Measure distance between two actors or two points. Returns total distance, per-axis distances, and direction vector. Critical for spacing verification."))
		.ReadOnly()
		.Idempotent()
		.StringArg(TEXT("from_actor"), TEXT("Start actor label (alternative to from_x/y/z)"))
		.NumberArg(TEXT("from_x"), TEXT("Start X (alternative to from_actor)"))
		.NumberArg(TEXT("from_y"), TEXT("Start Y"))
		.NumberArg(TEXT("from_z"), TEXT("Start Z"))
		.StringArg(TEXT("to_actor"), TEXT("End actor label (alternative to to_x/y/z)"))
		.NumberArg(TEXT("to_x"), TEXT("End X (alternative to to_actor)"))
		.NumberArg(TEXT("to_y"), TEXT("End Y"))
		.NumberArg(TEXT("to_z"), TEXT("End Z"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			FVector From, To;

			// Resolve "from"
			FString FromActor;
			if (Args->TryGetStringField(TEXT("from_actor"), FromActor))
			{
				AActor* Actor = FindActorByLabel(World, FromActor);
				if (!Actor) return FMCPToolResult::Error(FString::Printf(TEXT("From actor not found: %s"), *FromActor));
				From = Actor->GetActorLocation();
			}
			else if (Args->HasField(TEXT("from_x")))
			{
				From.X = Args->GetNumberField(TEXT("from_x"));
				From.Y = Args->HasField(TEXT("from_y")) ? Args->GetNumberField(TEXT("from_y")) : 0.0;
				From.Z = Args->HasField(TEXT("from_z")) ? Args->GetNumberField(TEXT("from_z")) : 0.0;
			}
			else
			{
				return FMCPToolResult::Error(TEXT("Provide either from_actor or from_x/y/z"));
			}

			// Resolve "to"
			FString ToActor;
			if (Args->TryGetStringField(TEXT("to_actor"), ToActor))
			{
				AActor* Actor = FindActorByLabel(World, ToActor);
				if (!Actor) return FMCPToolResult::Error(FString::Printf(TEXT("To actor not found: %s"), *ToActor));
				To = Actor->GetActorLocation();
			}
			else if (Args->HasField(TEXT("to_x")))
			{
				To.X = Args->GetNumberField(TEXT("to_x"));
				To.Y = Args->HasField(TEXT("to_y")) ? Args->GetNumberField(TEXT("to_y")) : 0.0;
				To.Z = Args->HasField(TEXT("to_z")) ? Args->GetNumberField(TEXT("to_z")) : 0.0;
			}
			else
			{
				return FMCPToolResult::Error(TEXT("Provide either to_actor or to_x/y/z"));
			}

			FVector Delta = To - From;
			double Distance = FVector::Dist(From, To);
			FVector Dir = Delta.GetSafeNormal();

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetNumberField(TEXT("distance"), Distance);
			Result->SetNumberField(TEXT("distance_x"), FMath::Abs(Delta.X));
			Result->SetNumberField(TEXT("distance_y"), FMath::Abs(Delta.Y));
			Result->SetNumberField(TEXT("distance_z"), FMath::Abs(Delta.Z));
			Result->SetObjectField(TEXT("delta"), MakeVectorJson(Delta));
			Result->SetObjectField(TEXT("direction"), MakeVectorJson(Dir));
			Result->SetObjectField(TEXT("from"), MakeVectorJson(From));
			Result->SetObjectField(TEXT("to"), MakeVectorJson(To));

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});

} // void RegisterAll

} // namespace MCPSpatialTools::Trace
