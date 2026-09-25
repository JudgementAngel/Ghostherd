// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "Tools/MCPSplineTools.h"
#include "MCPToolRegistry.h"
#include "MCPToolBuilder.h"
#include "MCPProtocol.h"
#include "Engine/World.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Components/SplineComponent.h"
#include "Components/SplineMeshComponent.h"
#include "Engine/StaticMesh.h"

namespace MCPSplineTools
{

static UWorld* GetEditorWorld()
{
	return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
}

static USplineComponent* FindSplineOnActor(const FString& ActorName)
{
	UWorld* World = GetEditorWorld();
	if (!World) return nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if ((*It)->GetActorLabel() == ActorName)
		{
			return (*It)->FindComponentByClass<USplineComponent>();
		}
	}
	return nullptr;
}

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// create_spline_actor - Spawn an actor with a spline component
	// ================================================================
	MCP_TOOL(Registry, "create_spline_actor")
		.Description(TEXT("Spawn a new actor with a USplineComponent. Creates an initial spline with the specified number of points evenly spaced along the X axis."))
		.Idempotent()
		.StringArg(TEXT("label"), TEXT("Actor label in the scene outliner"))
		.NumberArg(TEXT("x"), TEXT("X position (default: 0)"))
		.NumberArg(TEXT("y"), TEXT("Y position (default: 0)"))
		.NumberArg(TEXT("z"), TEXT("Z position (default: 0)"))
		.IntArg(TEXT("num_points"), TEXT("Number of initial spline points (default: 2)"))
		.NumberArg(TEXT("point_spacing"), TEXT("Distance between initial points along X axis (default: 500)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			FVector SpawnLocation(
				Args->HasField(TEXT("x")) ? Args->GetNumberField(TEXT("x")) : 0.0,
				Args->HasField(TEXT("y")) ? Args->GetNumberField(TEXT("y")) : 0.0,
				Args->HasField(TEXT("z")) ? Args->GetNumberField(TEXT("z")) : 0.0
			);

			int32 NumPoints = 2;
			if (Args->HasField(TEXT("num_points")))
			{
				NumPoints = FMath::Clamp((int32)Args->GetNumberField(TEXT("num_points")), 2, 100);
			}

			double PointSpacing = 500.0;
			if (Args->HasField(TEXT("point_spacing")))
			{
				PointSpacing = Args->GetNumberField(TEXT("point_spacing"));
			}

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Create Spline Actor")));

			FTransform SpawnTransform(FRotator::ZeroRotator, SpawnLocation);
			AActor* SplineActor = World->SpawnActor<AActor>(AActor::StaticClass(), SpawnTransform);
			if (!SplineActor)
			{
				GEditor->EndTransaction();
				return FMCPToolResult::Error(TEXT("Failed to spawn spline actor"));
			}

			USplineComponent* Spline = NewObject<USplineComponent>(SplineActor, TEXT("SplineComponent"));
			Spline->RegisterComponent();
			SplineActor->AddInstanceComponent(Spline);
			SplineActor->SetRootComponent(Spline);

			// Clear default points and add our own
			Spline->ClearSplinePoints(false);
			for (int32 i = 0; i < NumPoints; i++)
			{
				FVector PointLocation(i * PointSpacing, 0.0, 0.0);
				Spline->AddSplinePoint(PointLocation, ESplineCoordinateSpace::Local, false);
			}
			Spline->UpdateSpline();

			// Set label
			FString Label;
			if (Args->TryGetStringField(TEXT("label"), Label))
			{
				SplineActor->SetActorLabel(Label);
			}
			else
			{
				SplineActor->SetActorLabel(TEXT("SplineActor"));
			}

			GEditor->EndTransaction();

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Created spline actor '%s' at (%.1f, %.1f, %.1f) with %d points, spacing %.1f"),
				*SplineActor->GetActorLabel(), SpawnLocation.X, SpawnLocation.Y, SpawnLocation.Z,
				NumPoints, PointSpacing));
		});

	// ================================================================
	// add_spline_point - Add a new point to an existing spline
	// ================================================================
	MCP_TOOL(Registry, "add_spline_point")
		.Description(TEXT("Add a point to an existing spline at the specified local-space position. If index is omitted the point is appended at the end."))
		.StringArg(TEXT("actor_name"), TEXT("Label of the actor with a SplineComponent"), true)
		.NumberArg(TEXT("x"), TEXT("X position of the new point (local space)"), true)
		.NumberArg(TEXT("y"), TEXT("Y position of the new point (local space)"), true)
		.NumberArg(TEXT("z"), TEXT("Z position of the new point (local space)"), true)
		.IntArg(TEXT("index"), TEXT("Insert at this index. If omitted, appends to the end."))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString ActorName;
			if (!Args->TryGetStringField(TEXT("actor_name"), ActorName))
				return FMCPToolResult::Error(TEXT("actor_name is required"));

			USplineComponent* Spline = FindSplineOnActor(ActorName);
			if (!Spline) return FMCPToolResult::Error(FString::Printf(TEXT("No spline found on actor '%s'"), *ActorName));

			FVector PointLocation(
				Args->GetNumberField(TEXT("x")),
				Args->GetNumberField(TEXT("y")),
				Args->GetNumberField(TEXT("z"))
			);

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Add Spline Point")));
			Spline->Modify();

			if (Args->HasField(TEXT("index")))
			{
				int32 Index = (int32)Args->GetNumberField(TEXT("index"));
				Index = FMath::Clamp(Index, 0, Spline->GetNumberOfSplinePoints());
				Spline->AddSplinePointAtIndex(PointLocation, Index, ESplineCoordinateSpace::Local, false);
				Spline->UpdateSpline();

				GEditor->EndTransaction();
				return FMCPToolResult::Success(FString::Printf(
					TEXT("Inserted spline point at index %d on '%s' at (%.1f, %.1f, %.1f). Total points: %d"),
					Index, *ActorName, PointLocation.X, PointLocation.Y, PointLocation.Z,
					Spline->GetNumberOfSplinePoints()));
			}
			else
			{
				Spline->AddSplinePoint(PointLocation, ESplineCoordinateSpace::Local, false);
				Spline->UpdateSpline();

				int32 NewIndex = Spline->GetNumberOfSplinePoints() - 1;
				GEditor->EndTransaction();
				return FMCPToolResult::Success(FString::Printf(
					TEXT("Appended spline point at index %d on '%s' at (%.1f, %.1f, %.1f). Total points: %d"),
					NewIndex, *ActorName, PointLocation.X, PointLocation.Y, PointLocation.Z,
					Spline->GetNumberOfSplinePoints()));
			}
		});

	// ================================================================
	// set_spline_point - Modify an existing spline point
	// ================================================================
	MCP_TOOL(Registry, "set_spline_point")
		.Description(TEXT("Modify the position and/or tangents of an existing spline point. Only provided fields are changed; omitted fields keep their current values."))
		.Idempotent()
		.StringArg(TEXT("actor_name"), TEXT("Label of the actor with a SplineComponent"), true)
		.IntArg(TEXT("index"), TEXT("Index of the spline point to modify"), true)
		.NumberArg(TEXT("x"), TEXT("New X position (local space)"))
		.NumberArg(TEXT("y"), TEXT("New Y position (local space)"))
		.NumberArg(TEXT("z"), TEXT("New Z position (local space)"))
		.NumberArg(TEXT("arrive_tangent_x"), TEXT("Arrive tangent X component"))
		.NumberArg(TEXT("arrive_tangent_y"), TEXT("Arrive tangent Y component"))
		.NumberArg(TEXT("arrive_tangent_z"), TEXT("Arrive tangent Z component"))
		.NumberArg(TEXT("leave_tangent_x"), TEXT("Leave tangent X component"))
		.NumberArg(TEXT("leave_tangent_y"), TEXT("Leave tangent Y component"))
		.NumberArg(TEXT("leave_tangent_z"), TEXT("Leave tangent Z component"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString ActorName;
			if (!Args->TryGetStringField(TEXT("actor_name"), ActorName))
				return FMCPToolResult::Error(TEXT("actor_name is required"));

			USplineComponent* Spline = FindSplineOnActor(ActorName);
			if (!Spline) return FMCPToolResult::Error(FString::Printf(TEXT("No spline found on actor '%s'"), *ActorName));

			if (!Args->HasField(TEXT("index")))
				return FMCPToolResult::Error(TEXT("index is required"));

			int32 Index = (int32)Args->GetNumberField(TEXT("index"));
			if (Index < 0 || Index >= Spline->GetNumberOfSplinePoints())
			{
				return FMCPToolResult::Error(FString::Printf(TEXT("Index %d out of range [0, %d)"),
					Index, Spline->GetNumberOfSplinePoints()));
			}

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Set Spline Point")));
			Spline->Modify();

			// Update position if any coordinate provided
			if (Args->HasField(TEXT("x")) || Args->HasField(TEXT("y")) || Args->HasField(TEXT("z")))
			{
				FVector CurrentPos = Spline->GetLocationAtSplinePoint(Index, ESplineCoordinateSpace::Local);
				if (Args->HasField(TEXT("x"))) CurrentPos.X = Args->GetNumberField(TEXT("x"));
				if (Args->HasField(TEXT("y"))) CurrentPos.Y = Args->GetNumberField(TEXT("y"));
				if (Args->HasField(TEXT("z"))) CurrentPos.Z = Args->GetNumberField(TEXT("z"));
				Spline->SetLocationAtSplinePoint(Index, CurrentPos, ESplineCoordinateSpace::Local, false);
			}

			// Update arrive tangent
			if (Args->HasField(TEXT("arrive_tangent_x")) || Args->HasField(TEXT("arrive_tangent_y")) || Args->HasField(TEXT("arrive_tangent_z")))
			{
				FVector ArriveTangent = Spline->GetArriveTangentAtSplinePoint(Index, ESplineCoordinateSpace::Local);
				if (Args->HasField(TEXT("arrive_tangent_x"))) ArriveTangent.X = Args->GetNumberField(TEXT("arrive_tangent_x"));
				if (Args->HasField(TEXT("arrive_tangent_y"))) ArriveTangent.Y = Args->GetNumberField(TEXT("arrive_tangent_y"));
				if (Args->HasField(TEXT("arrive_tangent_z"))) ArriveTangent.Z = Args->GetNumberField(TEXT("arrive_tangent_z"));
				Spline->SetTangentAtSplinePoint(Index, ArriveTangent, ESplineCoordinateSpace::Local, false);
			}

			// Update leave tangent
			if (Args->HasField(TEXT("leave_tangent_x")) || Args->HasField(TEXT("leave_tangent_y")) || Args->HasField(TEXT("leave_tangent_z")))
			{
				FVector LeaveTangent = Spline->GetLeaveTangentAtSplinePoint(Index, ESplineCoordinateSpace::Local);
				if (Args->HasField(TEXT("leave_tangent_x"))) LeaveTangent.X = Args->GetNumberField(TEXT("leave_tangent_x"));
				if (Args->HasField(TEXT("leave_tangent_y"))) LeaveTangent.Y = Args->GetNumberField(TEXT("leave_tangent_y"));
				if (Args->HasField(TEXT("leave_tangent_z"))) LeaveTangent.Z = Args->GetNumberField(TEXT("leave_tangent_z"));
				Spline->SetTangentAtSplinePoint(Index, LeaveTangent, ESplineCoordinateSpace::Local, false);
			}

			Spline->UpdateSpline();
			GEditor->EndTransaction();

			FVector FinalPos = Spline->GetLocationAtSplinePoint(Index, ESplineCoordinateSpace::Local);
			return FMCPToolResult::Success(FString::Printf(
				TEXT("Updated spline point %d on '%s' to (%.1f, %.1f, %.1f)"),
				Index, *ActorName, FinalPos.X, FinalPos.Y, FinalPos.Z));
		});

	// ================================================================
	// remove_spline_point - Remove a point from a spline
	// ================================================================
	MCP_TOOL(Registry, "remove_spline_point")
		.Description(TEXT("Remove a spline point by index. Remaining points are re-indexed automatically."))
		.Destructive()
		.StringArg(TEXT("actor_name"), TEXT("Label of the actor with a SplineComponent"), true)
		.IntArg(TEXT("index"), TEXT("Index of the spline point to remove"), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString ActorName;
			if (!Args->TryGetStringField(TEXT("actor_name"), ActorName))
				return FMCPToolResult::Error(TEXT("actor_name is required"));

			USplineComponent* Spline = FindSplineOnActor(ActorName);
			if (!Spline) return FMCPToolResult::Error(FString::Printf(TEXT("No spline found on actor '%s'"), *ActorName));

			if (!Args->HasField(TEXT("index")))
				return FMCPToolResult::Error(TEXT("index is required"));

			int32 Index = (int32)Args->GetNumberField(TEXT("index"));
			if (Index < 0 || Index >= Spline->GetNumberOfSplinePoints())
			{
				return FMCPToolResult::Error(FString::Printf(TEXT("Index %d out of range [0, %d)"),
					Index, Spline->GetNumberOfSplinePoints()));
			}

			if (Spline->GetNumberOfSplinePoints() <= 2)
			{
				return FMCPToolResult::Error(TEXT("Cannot remove point: spline must have at least 2 points"));
			}

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Remove Spline Point")));
			Spline->Modify();

			Spline->RemoveSplinePoint(Index, false);
			Spline->UpdateSpline();

			GEditor->EndTransaction();

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Removed spline point %d from '%s'. Remaining points: %d"),
				Index, *ActorName, Spline->GetNumberOfSplinePoints()));
		});

	// ================================================================
	// get_spline_info - Return spline metadata and all points
	// ================================================================
	MCP_TOOL(Registry, "get_spline_info")
		.Description(TEXT("Get detailed information about a spline: point count, total length, closed-loop state, and all point positions with tangents."))
		.ReadOnly()
		.StringArg(TEXT("actor_name"), TEXT("Label of the actor with a SplineComponent"), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString ActorName;
			if (!Args->TryGetStringField(TEXT("actor_name"), ActorName))
				return FMCPToolResult::Error(TEXT("actor_name is required"));

			USplineComponent* Spline = FindSplineOnActor(ActorName);
			if (!Spline) return FMCPToolResult::Error(FString::Printf(TEXT("No spline found on actor '%s'"), *ActorName));

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("actor"), ActorName);
			Result->SetNumberField(TEXT("point_count"), Spline->GetNumberOfSplinePoints());
			Result->SetNumberField(TEXT("spline_length"), Spline->GetSplineLength());
			Result->SetBoolField(TEXT("is_closed_loop"), Spline->IsClosedLoop());

			TArray<TSharedPtr<FJsonValue>> PointsArray;
			for (int32 i = 0; i < Spline->GetNumberOfSplinePoints(); i++)
			{
				TSharedPtr<FJsonObject> PointObj = MakeShared<FJsonObject>();
				PointObj->SetNumberField(TEXT("index"), i);

				FVector Pos = Spline->GetLocationAtSplinePoint(i, ESplineCoordinateSpace::Local);
				PointObj->SetNumberField(TEXT("x"), Pos.X);
				PointObj->SetNumberField(TEXT("y"), Pos.Y);
				PointObj->SetNumberField(TEXT("z"), Pos.Z);

				FVector ArriveTangent = Spline->GetArriveTangentAtSplinePoint(i, ESplineCoordinateSpace::Local);
				TSharedPtr<FJsonObject> ArriveObj = MakeShared<FJsonObject>();
				ArriveObj->SetNumberField(TEXT("x"), ArriveTangent.X);
				ArriveObj->SetNumberField(TEXT("y"), ArriveTangent.Y);
				ArriveObj->SetNumberField(TEXT("z"), ArriveTangent.Z);
				PointObj->SetObjectField(TEXT("arrive_tangent"), ArriveObj);

				FVector LeaveTangent = Spline->GetLeaveTangentAtSplinePoint(i, ESplineCoordinateSpace::Local);
				TSharedPtr<FJsonObject> LeaveObj = MakeShared<FJsonObject>();
				LeaveObj->SetNumberField(TEXT("x"), LeaveTangent.X);
				LeaveObj->SetNumberField(TEXT("y"), LeaveTangent.Y);
				LeaveObj->SetNumberField(TEXT("z"), LeaveTangent.Z);
				PointObj->SetObjectField(TEXT("leave_tangent"), LeaveObj);

				// Include point type
				ESplinePointType::Type PointType = Spline->GetSplinePointType(i);
				FString TypeStr;
				switch (PointType)
				{
					case ESplinePointType::Linear:       TypeStr = TEXT("Linear"); break;
					case ESplinePointType::Curve:        TypeStr = TEXT("Curve"); break;
					case ESplinePointType::Constant:     TypeStr = TEXT("Constant"); break;
					case ESplinePointType::CurveClamped: TypeStr = TEXT("CurveClamped"); break;
					case ESplinePointType::CurveCustomTangent: TypeStr = TEXT("CurveCustomTangent"); break;
					default: TypeStr = TEXT("Unknown"); break;
				}
				PointObj->SetStringField(TEXT("type"), TypeStr);

				PointsArray.Add(MakeShared<FJsonValueObject>(PointObj));
			}

			Result->SetArrayField(TEXT("points"), PointsArray);
			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});

	// ================================================================
	// set_spline_closed - Toggle closed loop on/off
	// ================================================================
	MCP_TOOL(Registry, "set_spline_closed")
		.Description(TEXT("Set whether the spline forms a closed loop. When closed, the last point connects back to the first."))
		.Idempotent()
		.StringArg(TEXT("actor_name"), TEXT("Label of the actor with a SplineComponent"), true)
		.BoolArg(TEXT("closed"), TEXT("True to close the spline loop, false to open it"), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString ActorName;
			if (!Args->TryGetStringField(TEXT("actor_name"), ActorName))
				return FMCPToolResult::Error(TEXT("actor_name is required"));

			bool bClosed = false;
			if (!Args->TryGetBoolField(TEXT("closed"), bClosed))
				return FMCPToolResult::Error(TEXT("closed is required"));

			USplineComponent* Spline = FindSplineOnActor(ActorName);
			if (!Spline) return FMCPToolResult::Error(FString::Printf(TEXT("No spline found on actor '%s'"), *ActorName));

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Set Spline Closed Loop")));
			Spline->Modify();

			Spline->SetClosedLoop(bClosed, false);
			Spline->UpdateSpline();

			GEditor->EndTransaction();

			return FMCPToolResult::Success(FString::Printf(TEXT("Set spline on '%s' closed loop = %s"),
				*ActorName, bClosed ? TEXT("true") : TEXT("false")));
		});

	// ================================================================
	// set_spline_type - Set the point type (interpolation mode)
	// ================================================================
	MCP_TOOL(Registry, "set_spline_type")
		.Description(TEXT("Set the interpolation type of a spline point. Linear produces straight segments, Curve uses smooth Hermite interpolation, Constant holds the value, CurveClamped prevents overshoot."))
		.Idempotent()
		.StringArg(TEXT("actor_name"), TEXT("Label of the actor with a SplineComponent"), true)
		.IntArg(TEXT("index"), TEXT("Index of the spline point to modify"), true)
		.EnumArg(TEXT("type"), TEXT("Spline point interpolation type"),
			{ TEXT("Linear"), TEXT("Curve"), TEXT("Constant"), TEXT("CurveClamped") }, true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString ActorName;
			if (!Args->TryGetStringField(TEXT("actor_name"), ActorName))
				return FMCPToolResult::Error(TEXT("actor_name is required"));

			USplineComponent* Spline = FindSplineOnActor(ActorName);
			if (!Spline) return FMCPToolResult::Error(FString::Printf(TEXT("No spline found on actor '%s'"), *ActorName));

			if (!Args->HasField(TEXT("index")))
				return FMCPToolResult::Error(TEXT("index is required"));

			int32 Index = (int32)Args->GetNumberField(TEXT("index"));
			if (Index < 0 || Index >= Spline->GetNumberOfSplinePoints())
			{
				return FMCPToolResult::Error(FString::Printf(TEXT("Index %d out of range [0, %d)"),
					Index, Spline->GetNumberOfSplinePoints()));
			}

			FString TypeStr;
			if (!Args->TryGetStringField(TEXT("type"), TypeStr))
				return FMCPToolResult::Error(TEXT("type is required"));

			ESplinePointType::Type PointType;
			if (TypeStr == TEXT("Linear"))            PointType = ESplinePointType::Linear;
			else if (TypeStr == TEXT("Curve"))         PointType = ESplinePointType::Curve;
			else if (TypeStr == TEXT("Constant"))      PointType = ESplinePointType::Constant;
			else if (TypeStr == TEXT("CurveClamped"))  PointType = ESplinePointType::CurveClamped;
			else return FMCPToolResult::Error(FString::Printf(TEXT("Invalid spline point type: %s"), *TypeStr));

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Set Spline Point Type")));
			Spline->Modify();

			Spline->SetSplinePointType(Index, PointType, false);
			Spline->UpdateSpline();

			GEditor->EndTransaction();

			return FMCPToolResult::Success(FString::Printf(TEXT("Set spline point %d on '%s' to type '%s'"),
				Index, *ActorName, *TypeStr));
		});
}

} // namespace MCPSplineTools
