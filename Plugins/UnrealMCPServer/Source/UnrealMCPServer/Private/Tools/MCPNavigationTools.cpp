// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "Tools/MCPNavigationTools.h"
#include "MCPToolRegistry.h"
#include "MCPProtocol.h"
#include "MCPToolBuilder.h"

#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "NavigationSystem.h"
#include "NavMesh/NavMeshBoundsVolume.h"

namespace MCPNavigationTools
{

static UWorld* GetEditorWorld()
{
	if (GEditor)
	{
		return GEditor->GetEditorWorldContext().World();
	}
	return nullptr;
}

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// build_navigation - Trigger navmesh build
	// ================================================================
	MCP_TOOL(Registry, "build_navigation")
		.Description(TEXT("Trigger a navigation mesh build for the current level. Requires at least one NavMeshBoundsVolume in the level."))
		.LongRunning()
		.Idempotent()
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
			if (!NavSys) return FMCPToolResult::Error(TEXT("No navigation system found. Add a NavMeshBoundsVolume first."));

			// Check for bounds volumes
			bool bHasBounds = false;
			for (TActorIterator<ANavMeshBoundsVolume> It(World); It; ++It)
			{
				bHasBounds = true;
				break;
			}
			if (!bHasBounds)
			{
				return FMCPToolResult::Error(TEXT("No NavMeshBoundsVolume found. Add one to define the navigation area."));
			}

			NavSys->Build();

			return FMCPToolResult::Success(TEXT("Navigation mesh build triggered successfully."));
		});

	// ================================================================
	// query_navigation_path - Find path between two points
	// ================================================================
	MCP_TOOL(Registry, "query_navigation_path")
		.Description(TEXT("Find a navigation path between two world positions. Returns path points, total distance, and whether the path is complete."))
		.ReadOnly()
		.Idempotent()
		.NumberArg(TEXT("start_x"), TEXT("Start X position"), true)
		.NumberArg(TEXT("start_y"), TEXT("Start Y position"), true)
		.NumberArg(TEXT("start_z"), TEXT("Start Z position"), true)
		.NumberArg(TEXT("end_x"), TEXT("End X position"), true)
		.NumberArg(TEXT("end_y"), TEXT("End Y position"), true)
		.NumberArg(TEXT("end_z"), TEXT("End Z position"), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			// v4 (matrix-found): the declared-required args were never validated —
			// GetNumberField silently returns 0 on missing fields, so the handler
			// proceeded with garbage coordinates.
			static const TCHAR* RequiredFields[] = {
				TEXT("start_x"), TEXT("start_y"), TEXT("start_z"),
				TEXT("end_x"), TEXT("end_y"), TEXT("end_z") };
			for (const TCHAR* Field : RequiredFields)
			{
				if (!Args->HasTypedField<EJson::Number>(Field))
				{
					return FMCPToolResult::Error(FString::Printf(TEXT("%s is required (number)"), Field));
				}
			}

			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
			if (!NavSys) return FMCPToolResult::Error(TEXT("No navigation system found"));

			FVector Start(
				Args->GetNumberField(TEXT("start_x")),
				Args->GetNumberField(TEXT("start_y")),
				Args->GetNumberField(TEXT("start_z"))
			);
			FVector End(
				Args->GetNumberField(TEXT("end_x")),
				Args->GetNumberField(TEXT("end_y")),
				Args->GetNumberField(TEXT("end_z"))
			);

			// v4 (matrix-found EDITOR CRASH, present since v3): the default nav
			// data instance is null until a navmesh exists for this level, and it
			// was dereferenced unconditionally — instant access violation in any
			// level without a built navmesh.
			ANavigationData* NavData = NavSys->GetDefaultNavDataInstance();
			if (!NavData)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
					TEXT("No navigation data exists in this level."),
					TEXT("Add a NavMeshBoundsVolume covering the area and run build_navigation, then retry."));
			}

			FPathFindingQuery Query(nullptr, *NavData, Start, End);
			FPathFindingResult Result = NavSys->FindPathSync(Query);

			TSharedPtr<FJsonObject> Output = MakeShared<FJsonObject>();

			if (Result.IsSuccessful() && Result.Path.IsValid())
			{
				Output->SetBoolField(TEXT("path_found"), true);
				Output->SetBoolField(TEXT("is_partial"), Result.IsPartial());

				const TArray<FNavPathPoint>& PathPoints = Result.Path->GetPathPoints();

				TArray<TSharedPtr<FJsonValue>> Points;
				double TotalDist = 0.0;
				FVector PrevPoint = Start;

				for (int32 i = 0; i < PathPoints.Num(); i++)
				{
					FVector Pt = PathPoints[i].Location;

					if (i > 0)
					{
						TotalDist += FVector::Dist(PrevPoint, Pt);
					}
					PrevPoint = Pt;

					TSharedPtr<FJsonObject> PointObj = MakeShared<FJsonObject>();
					PointObj->SetNumberField(TEXT("x"), Pt.X);
					PointObj->SetNumberField(TEXT("y"), Pt.Y);
					PointObj->SetNumberField(TEXT("z"), Pt.Z);
					Points.Add(MakeShared<FJsonValueObject>(PointObj));
				}

				Output->SetArrayField(TEXT("path_points"), Points);
				Output->SetNumberField(TEXT("point_count"), PathPoints.Num());
				Output->SetNumberField(TEXT("total_distance"), TotalDist);
			}
			else
			{
				Output->SetBoolField(TEXT("path_found"), false);
				Output->SetStringField(TEXT("reason"), TEXT("No valid path found between the specified points"));
			}

			return FMCPToolResult::SuccessStructured(JsonToString(Output), Output);
		});

	// ================================================================
	// get_navigation_info - Get navmesh info
	// ================================================================
	MCP_TOOL(Registry, "get_navigation_info")
		.Description(TEXT("Get navigation system information: navmesh bounds, agent settings, and build status."))
		.ReadOnly()
		.Idempotent()
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);

			TSharedPtr<FJsonObject> Output = MakeShared<FJsonObject>();
			Output->SetBoolField(TEXT("has_navigation_system"), NavSys != nullptr);

			if (!NavSys)
			{
				return FMCPToolResult::SuccessStructured(JsonToString(Output), Output);
			}

			// Nav data instances
			TArray<TSharedPtr<FJsonValue>> NavDatas;
			for (ANavigationData* NavData : NavSys->NavDataSet)
			{
				if (!IsValid(NavData)) continue;

				TSharedPtr<FJsonObject> ND = MakeShared<FJsonObject>();
				ND->SetStringField(TEXT("name"), NavData->GetName());
				ND->SetStringField(TEXT("class"), NavData->GetClass()->GetName());
				NavDatas.Add(MakeShared<FJsonValueObject>(ND));
			}
			Output->SetArrayField(TEXT("nav_data"), NavDatas);

			// Count bounds volumes
			int32 BoundsCount = 0;
			for (TActorIterator<ANavMeshBoundsVolume> It(World); It; ++It)
			{
				BoundsCount++;
			}
			Output->SetNumberField(TEXT("bounds_volume_count"), BoundsCount);

			// Agent properties
			const FNavDataConfig& DefaultConfig = NavSys->GetDefaultSupportedAgent();
			TSharedPtr<FJsonObject> Agent = MakeShared<FJsonObject>();
			Agent->SetStringField(TEXT("name"), DefaultConfig.Name.ToString());
			Agent->SetNumberField(TEXT("agent_radius"), DefaultConfig.AgentRadius);
			Agent->SetNumberField(TEXT("agent_height"), DefaultConfig.AgentHeight);
			Agent->SetNumberField(TEXT("agent_step_height"), DefaultConfig.AgentStepHeight);
			Output->SetObjectField(TEXT("default_agent"), Agent);

			return FMCPToolResult::SuccessStructured(JsonToString(Output), Output);
		});
}

} // namespace MCPNavigationTools
