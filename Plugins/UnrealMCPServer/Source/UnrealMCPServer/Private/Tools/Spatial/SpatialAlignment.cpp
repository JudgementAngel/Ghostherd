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

namespace MCPSpatialTools::Alignment
{

using namespace MCPSpatialTools::Common;

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// align_actors - Align/snap actors relative to each other or grid
	// ================================================================
	MCP_TOOL(Registry, "align_actors")
		.Description(TEXT("Align/snap actors relative to each other (min/max/center on any axis) or snap them all to a grid."))
		.Idempotent()
		.StringArrayArg(TEXT("actor_names"), TEXT("Array of actor labels to align"), true)
		.EnumArg(TEXT("align_mode"), TEXT("Alignment mode"),
			{ TEXT("min_x"), TEXT("max_x"), TEXT("center_x"),
			  TEXT("min_y"), TEXT("max_y"), TEXT("center_y"),
			  TEXT("min_z"), TEXT("max_z"), TEXT("center_z"),
			  TEXT("grid") }, true)
		.NumberArg(TEXT("grid_size"), TEXT("Grid cell size for 'grid' mode (default: 100)"))
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
			if (Names.Num() < 1) return FMCPToolResult::Error(TEXT("At least one actor name required"));

			FString AlignMode;
			if (!Args->TryGetStringField(TEXT("align_mode"), AlignMode))
				return FMCPToolResult::Error(TEXT("align_mode is required"));

			// v4 Phase 0 (bug fix): an unrecognized mode previously fell through every
			// branch and reported success while moving nothing. Validate up front.
			static const TSet<FString> ValidModes = {
				TEXT("min_x"), TEXT("max_x"), TEXT("center_x"),
				TEXT("min_y"), TEXT("max_y"), TEXT("center_y"),
				TEXT("min_z"), TEXT("max_z"), TEXT("center_z"),
				TEXT("grid") };
			if (!ValidModes.Contains(AlignMode))
			{
				return FMCPToolResult::ErrorStructured(EMCPError::OutOfRange,
					FString::Printf(TEXT("Unknown align_mode '%s'."), *AlignMode),
					TEXT("Valid modes: min_x/max_x/center_x, min_y/max_y/center_y, min_z/max_z/center_z, grid."));
			}

			double GridSize = 100.0;
			if (Args->HasField(TEXT("grid_size"))) GridSize = Args->GetNumberField(TEXT("grid_size"));
			if (GridSize <= 0) GridSize = 100.0;

			// Find all actors
			TArray<AActor*> Actors;
			for (const auto& Val : Names)
			{
				FString Name;
				if (Val->TryGetString(Name))
				{
					AActor* Actor = FindActorByLabel(World, Name);
					if (Actor) Actors.Add(Actor);
				}
			}

			if (Actors.Num() == 0) return FMCPToolResult::Error(TEXT("No valid actors found"));

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Align Actors")));

			if (AlignMode == TEXT("grid"))
			{
				// Snap each actor to nearest grid point
				for (AActor* Actor : Actors)
				{
					Actor->Modify();
					FVector Loc = Actor->GetActorLocation();
					Loc.X = FMath::RoundToDouble(Loc.X / GridSize) * GridSize;
					Loc.Y = FMath::RoundToDouble(Loc.Y / GridSize) * GridSize;
					Loc.Z = FMath::RoundToDouble(Loc.Z / GridSize) * GridSize;
					Actor->SetActorLocation(Loc);
				}
			}
			else
			{
				// Calculate alignment target from first actor
				// For min/max modes, find the extreme value among all actors
				double AlignTarget = 0.0;

				if (AlignMode == TEXT("min_x") || AlignMode == TEXT("max_x") || AlignMode == TEXT("center_x") ||
					AlignMode == TEXT("min_y") || AlignMode == TEXT("max_y") || AlignMode == TEXT("center_y") ||
					AlignMode == TEXT("min_z") || AlignMode == TEXT("max_z") || AlignMode == TEXT("center_z"))
				{
					bool bFirst = true;
					for (AActor* Actor : Actors)
					{
						FVector Origin, BoxExtent;
						Actor->GetActorBounds(false, Origin, BoxExtent);

						double Val = 0.0;
						if (AlignMode == TEXT("min_x")) Val = Origin.X - BoxExtent.X;
						else if (AlignMode == TEXT("max_x")) Val = Origin.X + BoxExtent.X;
						else if (AlignMode == TEXT("center_x")) Val = Origin.X;
						else if (AlignMode == TEXT("min_y")) Val = Origin.Y - BoxExtent.Y;
						else if (AlignMode == TEXT("max_y")) Val = Origin.Y + BoxExtent.Y;
						else if (AlignMode == TEXT("center_y")) Val = Origin.Y;
						else if (AlignMode == TEXT("min_z")) Val = Origin.Z - BoxExtent.Z;
						else if (AlignMode == TEXT("max_z")) Val = Origin.Z + BoxExtent.Z;
						else if (AlignMode == TEXT("center_z")) Val = Origin.Z;

						if (bFirst)
						{
							AlignTarget = Val;
							bFirst = false;
						}
						else
						{
							if (AlignMode.StartsWith(TEXT("min"))) AlignTarget = FMath::Min(AlignTarget, Val);
							else if (AlignMode.StartsWith(TEXT("max"))) AlignTarget = FMath::Max(AlignTarget, Val);
							else AlignTarget = (AlignTarget + Val) / 2.0; // average for center
						}
					}

					// For center mode, compute true average
					if (AlignMode.StartsWith(TEXT("center")))
					{
						double Sum = 0.0;
						for (AActor* Actor : Actors)
						{
							FVector Origin, BoxExtent;
							Actor->GetActorBounds(false, Origin, BoxExtent);
							if (AlignMode == TEXT("center_x")) Sum += Origin.X;
							else if (AlignMode == TEXT("center_y")) Sum += Origin.Y;
							else if (AlignMode == TEXT("center_z")) Sum += Origin.Z;
						}
						AlignTarget = Sum / Actors.Num();
					}

					// Apply alignment
					for (AActor* Actor : Actors)
					{
						Actor->Modify();
						FVector Loc = Actor->GetActorLocation();
						FVector Origin, BoxExtent;
						Actor->GetActorBounds(false, Origin, BoxExtent);

						if (AlignMode == TEXT("min_x"))
						{
							double CurrentMin = Origin.X - BoxExtent.X;
							Loc.X += AlignTarget - CurrentMin;
						}
						else if (AlignMode == TEXT("max_x"))
						{
							double CurrentMax = Origin.X + BoxExtent.X;
							Loc.X += AlignTarget - CurrentMax;
						}
						else if (AlignMode == TEXT("center_x"))
						{
							Loc.X += AlignTarget - Origin.X;
						}
						else if (AlignMode == TEXT("min_y"))
						{
							double CurrentMin = Origin.Y - BoxExtent.Y;
							Loc.Y += AlignTarget - CurrentMin;
						}
						else if (AlignMode == TEXT("max_y"))
						{
							double CurrentMax = Origin.Y + BoxExtent.Y;
							Loc.Y += AlignTarget - CurrentMax;
						}
						else if (AlignMode == TEXT("center_y"))
						{
							Loc.Y += AlignTarget - Origin.Y;
						}
						else if (AlignMode == TEXT("min_z"))
						{
							double CurrentMin = Origin.Z - BoxExtent.Z;
							Loc.Z += AlignTarget - CurrentMin;
						}
						else if (AlignMode == TEXT("max_z"))
						{
							double CurrentMax = Origin.Z + BoxExtent.Z;
							Loc.Z += AlignTarget - CurrentMax;
						}
						else if (AlignMode == TEXT("center_z"))
						{
							Loc.Z += AlignTarget - Origin.Z;
						}

						Actor->SetActorLocation(Loc);
					}
				}
			}

			GEditor->EndTransaction();

			// Build result with new positions
			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("align_mode"), AlignMode);
			Result->SetNumberField(TEXT("actor_count"), Actors.Num());

			TArray<TSharedPtr<FJsonValue>> PositionsArray;
			for (AActor* Actor : Actors)
			{
				TSharedPtr<FJsonObject> PosObj = MakeShared<FJsonObject>();
				PosObj->SetStringField(TEXT("name"), Actor->GetActorLabel());
				PosObj->SetObjectField(TEXT("position"), MakeVectorJson(Actor->GetActorLocation()));
				PositionsArray.Add(MakeShared<FJsonValueObject>(PosObj));
			}
			Result->SetArrayField(TEXT("new_positions"), PositionsArray);

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});

	// ================================================================
	// stack_actors - Stack actors along an axis with proper spacing
	// ================================================================
	MCP_TOOL(Registry, "stack_actors")
		.Description(TEXT("Stack actors on top of / next to each other with proper spacing based on their bounds. Critical for building walls, stacking crates, assembling modular pieces."))
		.StringArrayArg(TEXT("actor_names"), TEXT("Array of actor labels to stack, in order"), true)
		.EnumArg(TEXT("direction"), TEXT("Stacking direction"),
			{ TEXT("up"), TEXT("right"), TEXT("forward") }, true)
		.NumberArg(TEXT("gap"), TEXT("Gap between stacked actors in units (default: 0)"))
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
			if (Names.Num() < 2) return FMCPToolResult::Error(TEXT("At least two actor names required for stacking"));

			FString Direction;
			if (!Args->TryGetStringField(TEXT("direction"), Direction))
				return FMCPToolResult::Error(TEXT("direction is required"));

			double Gap = 0.0;
			if (Args->HasField(TEXT("gap"))) Gap = Args->GetNumberField(TEXT("gap"));

			// Find actors in order
			TArray<AActor*> Actors;
			for (const auto& Val : Names)
			{
				FString Name;
				if (Val->TryGetString(Name))
				{
					AActor* Actor = FindActorByLabel(World, Name);
					if (Actor) Actors.Add(Actor);
				}
			}

			if (Actors.Num() < 2) return FMCPToolResult::Error(TEXT("Need at least 2 valid actors to stack"));

			// Determine axis: up=Z, right=Y, forward=X
			int32 Axis = 2; // Z by default (up)
			if (Direction == TEXT("right")) Axis = 1; // Y
			else if (Direction == TEXT("forward")) Axis = 0; // X

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Stack Actors")));

			// First actor stays in place. Each subsequent actor is placed after the previous one.
			for (int32 i = 1; i < Actors.Num(); i++)
			{
				AActor* PrevActor = Actors[i - 1];
				AActor* CurrActor = Actors[i];

				FVector PrevOrigin, PrevExtent;
				PrevActor->GetActorBounds(false, PrevOrigin, PrevExtent);

				FVector CurrOrigin, CurrExtent;
				CurrActor->GetActorBounds(false, CurrOrigin, CurrExtent);

				// Calculate where the current actor's bounds edge should start
				// (after the previous actor's bounds edge + gap)
				double PrevMax = 0.0;
				double CurrMin = 0.0;
				double CurrCenter = 0.0;

				if (Axis == 0) // X (forward)
				{
					PrevMax = PrevOrigin.X + PrevExtent.X;
					CurrMin = CurrOrigin.X - CurrExtent.X;
					CurrCenter = CurrOrigin.X;
				}
				else if (Axis == 1) // Y (right)
				{
					PrevMax = PrevOrigin.Y + PrevExtent.Y;
					CurrMin = CurrOrigin.Y - CurrExtent.Y;
					CurrCenter = CurrOrigin.Y;
				}
				else // Z (up)
				{
					PrevMax = PrevOrigin.Z + PrevExtent.Z;
					CurrMin = CurrOrigin.Z - CurrExtent.Z;
					CurrCenter = CurrOrigin.Z;
				}

				double TargetMin = PrevMax + Gap;
				double Offset = TargetMin - CurrMin;

				CurrActor->Modify();
				FVector NewLoc = CurrActor->GetActorLocation();
				if (Axis == 0) NewLoc.X += Offset;
				else if (Axis == 1) NewLoc.Y += Offset;
				else NewLoc.Z += Offset;

				CurrActor->SetActorLocation(NewLoc);
			}

			GEditor->EndTransaction();

			// Build result
			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("direction"), Direction);
			Result->SetNumberField(TEXT("gap"), Gap);
			Result->SetNumberField(TEXT("actor_count"), Actors.Num());

			TArray<TSharedPtr<FJsonValue>> PositionsArray;
			for (AActor* Actor : Actors)
			{
				TSharedPtr<FJsonObject> PosObj = MakeShared<FJsonObject>();
				PosObj->SetStringField(TEXT("name"), Actor->GetActorLabel());
				PosObj->SetObjectField(TEXT("position"), MakeVectorJson(Actor->GetActorLocation()));

				FVector Origin, Extent;
				Actor->GetActorBounds(false, Origin, Extent);
				PosObj->SetObjectField(TEXT("bounds_size"), MakeVectorJson(Extent * 2.0));

				PositionsArray.Add(MakeShared<FJsonValueObject>(PosObj));
			}
			Result->SetArrayField(TEXT("new_positions"), PositionsArray);

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});

} // void RegisterAll

} // namespace MCPSpatialTools::Alignment
