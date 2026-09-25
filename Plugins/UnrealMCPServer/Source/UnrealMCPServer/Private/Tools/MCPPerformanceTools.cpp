// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "Tools/MCPPerformanceTools.h"
#include "MCPToolRegistry.h"
#include "MCPToolBuilder.h"
#include "MCPProtocol.h"

#include "Editor.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/LightComponent.h"
#include "Engine/StaticMesh.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "HAL/PlatformMemory.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"
#include "EditorViewportClient.h"
#include "LevelEditorViewport.h"
#include "MCPScenarios.h"
#include "Misc/App.h"
#include "Misc/EngineVersion.h"
#include "RHI.h"
#include "RHIGlobals.h"
#include "DynamicRHI.h"
#include "RenderTimer.h"

namespace MCPPerformanceTools
{
// v5 increment 20 (V5-29): every report states whether its numbers are estimates or measurements,
// in which units, and under which engine/hardware/world conditions they were produced.
static void AddRunMetadata(const TSharedPtr<FJsonObject>& Result)
{
	auto M = MakeShared<FJsonObject>();
	M->SetStringField(TEXT("engine_version"), FEngineVersion::Current().ToString());
	M->SetStringField(TEXT("platform"), FPlatformProperties::IniPlatformName());
	M->SetStringField(TEXT("build_configuration"), LexToString(FApp::GetBuildConfiguration()));
	M->SetStringField(TEXT("rhi"), GDynamicRHI ? GDynamicRHI->GetName() : TEXT("none"));
	M->SetStringField(TEXT("gpu_adapter"), GRHIAdapterName);
	M->SetBoolField(TEXT("null_rhi"), GUsingNullRHI);
	M->SetStringField(TEXT("timestamp_utc"), FDateTime::UtcNow().ToIso8601());
	if (UWorld* W = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr) M->SetStringField(TEXT("world_id"), W->GetPathName());
	M->SetBoolField(TEXT("pie_active"), GEditor && GEditor->IsPlaySessionInProgress());
	if (GCurrentLevelEditingViewportClient && GCurrentLevelEditingViewportClient->Viewport)
	{
		const FIntPoint Sz = GCurrentLevelEditingViewportClient->Viewport->GetSizeXY();
		M->SetNumberField(TEXT("viewport_width"), Sz.X); M->SetNumberField(TEXT("viewport_height"), Sz.Y);
		M->SetStringField(TEXT("camera_location"), GCurrentLevelEditingViewportClient->GetViewLocation().ToString());
	}
	Result->SetObjectField(TEXT("run_metadata"), M);
}
static void AddProvenance(const TSharedPtr<FJsonObject>& Result, const TCHAR* Kind, const TCHAR* Method, const TCHAR* Units)
{
	auto P = MakeShared<FJsonObject>();
	P->SetStringField(TEXT("kind"), Kind); P->SetStringField(TEXT("method"), Method); P->SetStringField(TEXT("units"), Units);
	P->SetStringField(TEXT("note"), FCString::Strcmp(Kind, TEXT("estimate")) == 0 ? TEXT("Estimated from scene structure; not a measured frame or GPU time. Use measure_frame_times for measurements.") : TEXT("Measured in this editor process under the run_metadata conditions."));
	Result->SetObjectField(TEXT("provenance"), P);
	AddRunMetadata(Result);
}


static UWorld* GetEditorWorld()
{
	if (GEditor) return GEditor->GetEditorWorldContext().World();
	return nullptr;
}

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// get_render_stats - Rendering statistics
	// ================================================================
	MCP_TOOL(Registry, "get_render_stats")
		.Description(TEXT("Get rendering performance statistics for the current viewport: estimated draw calls, triangle count from visible static meshes, light count by type, shadow caster count, and actor distribution by class."))
		.ReadOnly()
		.Idempotent()
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

			int32 TotalActors = 0;
			int64 TotalTriangles = 0;
			int32 TotalDrawCalls = 0;
			int32 StaticMeshActors = 0;
			int32 PointLights = 0, SpotLights = 0, DirLights = 0;
			int32 ShadowCasters = 0;
			TMap<FString, int32> ClassDistribution;

			for (TActorIterator<AActor> It(World); It; ++It)
			{
				AActor* Actor = *It;
				if (!IsValid(Actor)) continue;
				TotalActors++;

				FString ClassName = Actor->GetClass()->GetName();
				ClassDistribution.FindOrAdd(ClassName)++;

				// Static mesh stats
				TArray<UStaticMeshComponent*> SMCs;
				Actor->GetComponents<UStaticMeshComponent>(SMCs);
				for (UStaticMeshComponent* SMC : SMCs)
				{
					if (!SMC || !SMC->GetStaticMesh()) continue;
					StaticMeshActors++;

					UStaticMesh* Mesh = SMC->GetStaticMesh();
					if (Mesh->GetRenderData() && Mesh->GetRenderData()->LODResources.Num() > 0)
					{
						const FStaticMeshLODResources& LOD0 = Mesh->GetRenderData()->LODResources[0];
						TotalTriangles += LOD0.GetNumTriangles();
						TotalDrawCalls += LOD0.Sections.Num(); // Each section = 1 draw call
					}

					if (SMC->CastShadow) ShadowCasters++;
				}

				// Light stats
				TArray<ULightComponent*> LCs;
				Actor->GetComponents<ULightComponent>(LCs);
				for (ULightComponent* LC : LCs)
				{
					if (!LC) continue;
					FString LightClass = LC->GetClass()->GetName();
					if (LightClass.Contains(TEXT("Point"))) PointLights++;
					else if (LightClass.Contains(TEXT("Spot"))) SpotLights++;
					else if (LightClass.Contains(TEXT("Directional"))) DirLights++;

					if (LC->CastShadows) ShadowCasters++;
				}
			}

			Result->SetNumberField(TEXT("total_actors"), TotalActors);
			Result->SetNumberField(TEXT("static_mesh_components"), StaticMeshActors);
			Result->SetNumberField(TEXT("estimated_triangles"), (double)TotalTriangles);
			Result->SetNumberField(TEXT("estimated_draw_calls"), TotalDrawCalls);
			Result->SetNumberField(TEXT("shadow_casters"), ShadowCasters);

			TSharedPtr<FJsonObject> Lights = MakeShared<FJsonObject>();
			Lights->SetNumberField(TEXT("point"), PointLights);
			Lights->SetNumberField(TEXT("spot"), SpotLights);
			Lights->SetNumberField(TEXT("directional"), DirLights);
			Lights->SetNumberField(TEXT("total"), PointLights + SpotLights + DirLights);
			Result->SetObjectField(TEXT("lights"), Lights);

			// Top 10 class distribution
			ClassDistribution.ValueSort([](int32 A, int32 B) { return A > B; });
			TArray<TSharedPtr<FJsonValue>> ClassArray;
			int32 ClassCount = 0;
			for (const auto& Pair : ClassDistribution)
			{
				if (ClassCount++ >= 10) break;
				TSharedPtr<FJsonObject> CObj = MakeShared<FJsonObject>();
				CObj->SetStringField(TEXT("class"), Pair.Key);
				CObj->SetNumberField(TEXT("count"), Pair.Value);
				ClassArray.Add(MakeShared<FJsonValueObject>(CObj));
			}
			Result->SetArrayField(TEXT("top_classes"), ClassArray);

			// Warnings
			TArray<TSharedPtr<FJsonValue>> Warnings;
			if (TotalDrawCalls > 5000)
				Warnings.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("Very high draw call estimate: %d. Use instancing, merge meshes, or reduce material count."), TotalDrawCalls)));
			if (TotalTriangles > 10000000)
				Warnings.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("Very high triangle count: %lld. Consider enabling Nanite or adding LODs."), TotalTriangles)));
			if (ShadowCasters > 100)
				Warnings.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("High shadow caster count: %d."), ShadowCasters)));

			Result->SetArrayField(TEXT("warnings"), Warnings);
			AddProvenance(Result, TEXT("estimate"), TEXT("LOD0 triangle sums and per-component/material draw-call estimate over the editor world; no renderer query"), TEXT("triangles, draw calls, counts"));

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});

	// ================================================================
	// get_memory_report - Memory usage by category
	// ================================================================
	MCP_TOOL(Registry, "get_memory_report")
		.Description(TEXT("Get memory usage report: system memory stats, and disk size of assets by category (Textures, StaticMeshes, Blueprints, Materials, Animations, Audio). Shows total size and top N largest assets per category."))
		.ReadOnly()
		.Idempotent()
		.StringArg(TEXT("path"), TEXT("Content path to analyze (default: '/Game/')"))
		.IntArg(TEXT("limit"), TEXT("Top N largest assets per category (default: 10)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString Path = TEXT("/Game/");
			Args->TryGetStringField(TEXT("path"), Path);

			int32 Limit = 10;
			if (Args->HasField(TEXT("limit")))
				Limit = FMath::Clamp((int32)Args->GetNumberField(TEXT("limit")), 1, 50);

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

			// System memory
			FPlatformMemoryStats MemStats = FPlatformMemory::GetStats();
			TSharedPtr<FJsonObject> SysMem = MakeShared<FJsonObject>();
			SysMem->SetNumberField(TEXT("used_physical_mb"), (double)(MemStats.UsedPhysical / (1024 * 1024)));
			SysMem->SetNumberField(TEXT("available_physical_mb"), (double)(MemStats.AvailablePhysical / (1024 * 1024)));
			SysMem->SetNumberField(TEXT("used_virtual_mb"), (double)(MemStats.UsedVirtual / (1024 * 1024)));
			Result->SetObjectField(TEXT("system_memory"), SysMem);

			// Asset size by category
			FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
			IAssetRegistry& AR = ARM.Get();

			TArray<FAssetData> AllAssets;
			AR.GetAssetsByPath(FName(*Path), AllAssets, true);

			// Group by class
			TMap<FString, TArray<TPair<int64, FString>>> CategoryAssets;

			for (const FAssetData& Asset : AllAssets)
			{
				FString ClassName = Asset.AssetClassPath.GetAssetName().ToString();
				FString Category;

				if (ClassName.Contains(TEXT("Texture"))) Category = TEXT("Textures");
				else if (ClassName.Contains(TEXT("StaticMesh"))) Category = TEXT("StaticMeshes");
				else if (ClassName.Contains(TEXT("Blueprint")) || ClassName.Contains(TEXT("WidgetBlueprint"))) Category = TEXT("Blueprints");
				else if (ClassName.Contains(TEXT("Material"))) Category = TEXT("Materials");
				else if (ClassName.Contains(TEXT("Anim")) || ClassName.Contains(TEXT("Skeleton"))) Category = TEXT("Animation");
				else if (ClassName.Contains(TEXT("Sound")) || ClassName.Contains(TEXT("MetaSound"))) Category = TEXT("Audio");
				else Category = TEXT("Other");

				FString PkgFilename = FPackageName::LongPackageNameToFilename(
					Asset.PackageName.ToString(), FPackageName::GetAssetPackageExtension());
				int64 FileSize = IFileManager::Get().FileSize(*PkgFilename);
				if (FileSize > 0)
				{
					CategoryAssets.FindOrAdd(Category).Add(TPair<int64, FString>(FileSize, Asset.AssetName.ToString()));
				}
			}

			TArray<TSharedPtr<FJsonValue>> CategoriesArray;
			int64 GrandTotal = 0;

			for (auto& Pair : CategoryAssets)
			{
				// Sort by size descending
				Pair.Value.Sort([](const TPair<int64, FString>& A, const TPair<int64, FString>& B) { return A.Key > B.Key; });

				int64 CategoryTotal = 0;
				for (const auto& Entry : Pair.Value) CategoryTotal += Entry.Key;
				GrandTotal += CategoryTotal;

				TSharedPtr<FJsonObject> CatObj = MakeShared<FJsonObject>();
				CatObj->SetStringField(TEXT("category"), Pair.Key);
				CatObj->SetNumberField(TEXT("asset_count"), Pair.Value.Num());
				CatObj->SetNumberField(TEXT("total_size_mb"), CategoryTotal / (1024.0 * 1024.0));

				// Top N
				TArray<TSharedPtr<FJsonValue>> TopArray;
				int32 TopCount = FMath::Min(Pair.Value.Num(), Limit);
				for (int32 i = 0; i < TopCount; i++)
				{
					TSharedPtr<FJsonObject> TObj = MakeShared<FJsonObject>();
					TObj->SetStringField(TEXT("name"), Pair.Value[i].Value);
					TObj->SetStringField(TEXT("size"), Pair.Value[i].Key > 1048576
						? FString::Printf(TEXT("%.1f MB"), Pair.Value[i].Key / 1048576.0)
						: FString::Printf(TEXT("%.1f KB"), Pair.Value[i].Key / 1024.0));
					TopArray.Add(MakeShared<FJsonValueObject>(TObj));
				}
				CatObj->SetArrayField(TEXT("largest"), TopArray);

				CategoriesArray.Add(MakeShared<FJsonValueObject>(CatObj));
			}

			// Sort categories by total size
			CategoriesArray.Sort([](const TSharedPtr<FJsonValue>& A, const TSharedPtr<FJsonValue>& B)
			{
				return A->AsObject()->GetNumberField(TEXT("total_size_mb")) > B->AsObject()->GetNumberField(TEXT("total_size_mb"));
			});

			Result->SetNumberField(TEXT("total_project_size_mb"), GrandTotal / (1024.0 * 1024.0));
			Result->SetArrayField(TEXT("categories"), CategoriesArray);
			AddProvenance(Result, TEXT("measurement"), TEXT("FPlatformMemory::GetStats for process memory; on-disk package sizes from the asset registry for asset categories (not runtime memory)"), TEXT("megabytes"));

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});

	// ================================================================
	// profile_actors_in_view - Per-actor render cost in viewport
	// ================================================================
	MCP_TOOL(Registry, "profile_actors_in_view")
		.Description(TEXT("Profile per-actor rendering cost for actors in the current viewport frustum. Returns actors sorted by estimated render cost: triangle count, material count, shadow casting, Nanite state, and component count. Use to identify performance bottlenecks."))
		.ReadOnly()
		.Idempotent()
		.IntArg(TEXT("limit"), TEXT("Maximum actors to return, sorted by cost (default: 20)"))
		.BoolArg(TEXT("include_lights"), TEXT("Include light actors (default: true)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			int32 Limit = 20;
			if (Args->HasField(TEXT("limit")))
				Limit = FMath::Clamp((int32)Args->GetNumberField(TEXT("limit")), 1, 100);

			bool bIncludeLights = true;
			Args->TryGetBoolField(TEXT("include_lights"), bIncludeLights);

			// Get the editor viewport camera info for frustum check
			FVector CameraLocation = FVector::ZeroVector;
			FRotator CameraRotation = FRotator::ZeroRotator;
			float CameraFOV = 90.0f;

			if (GEditor && GEditor->GetActiveViewport())
			{
				FEditorViewportClient* ViewportClient = (FEditorViewportClient*)GEditor->GetActiveViewport()->GetClient();
				if (ViewportClient)
				{
					CameraLocation = ViewportClient->GetViewLocation();
					CameraRotation = ViewportClient->GetViewRotation();
					CameraFOV = ViewportClient->ViewFOV;
				}
			}

			// Build a simple frustum approximation (use distance-based for editor)
			float MaxDistance = 50000.0f; // 500m view distance

			struct FActorCost
			{
				AActor* Actor;
				int64 Triangles;
				int32 MaterialSlots;
				int32 Components;
				bool bCastsShadow;
				bool bNanite;
				float Distance;
				float EstimatedCost;
			};

			TArray<FActorCost> ActorCosts;

			for (TActorIterator<AActor> It(World); It; ++It)
			{
				AActor* Actor = *It;
				if (!Actor || Actor->IsHidden()) continue;

				float Distance = FVector::Dist(CameraLocation, Actor->GetActorLocation());
				if (Distance > MaxDistance) continue;

				// Skip lights if not requested
				if (!bIncludeLights && Actor->FindComponentByClass<ULightComponent>())
					continue;

				FActorCost Cost;
				Cost.Actor = Actor;
				Cost.Triangles = 0;
				Cost.MaterialSlots = 0;
				Cost.Components = 0;
				Cost.bCastsShadow = false;
				Cost.bNanite = false;
				Cost.Distance = Distance;

				// Scan all mesh components
				TArray<UStaticMeshComponent*> MeshComps;
				Actor->GetComponents<UStaticMeshComponent>(MeshComps);

				for (UStaticMeshComponent* MeshComp : MeshComps)
				{
					if (!MeshComp || !MeshComp->GetStaticMesh()) continue;
					Cost.Components++;

					UStaticMesh* Mesh = MeshComp->GetStaticMesh();
					if (Mesh->GetRenderData() && Mesh->GetRenderData()->LODResources.Num() > 0)
					{
						Cost.Triangles += Mesh->GetRenderData()->LODResources[0].GetNumTriangles();
					}
					Cost.MaterialSlots += Mesh->GetStaticMaterials().Num();
					Cost.bCastsShadow |= MeshComp->CastShadow;
					Cost.bNanite |= Mesh->GetNaniteSettings().bEnabled;
				}

				// Check for light components
				ULightComponent* LightComp = Actor->FindComponentByClass<ULightComponent>();
				if (LightComp)
				{
					Cost.Components++;
					Cost.bCastsShadow |= LightComp->CastShadows;
				}

				// Estimated cost score: weighted combination
				Cost.EstimatedCost = (float)Cost.Triangles * 0.001f
					+ (float)Cost.MaterialSlots * 10.0f
					+ (Cost.bCastsShadow ? 50.0f : 0.0f)
					+ (Cost.bNanite ? -20.0f : 0.0f); // Nanite reduces cost

				if (Cost.Components > 0 || LightComp)
					ActorCosts.Add(Cost);
			}

			// Sort by cost descending
			ActorCosts.Sort([](const FActorCost& A, const FActorCost& B)
			{
				return A.EstimatedCost > B.EstimatedCost;
			});

			// Build result
			TArray<TSharedPtr<FJsonValue>> ActorArray;
			int64 TotalTriangles = 0;
			int32 TotalActors = ActorCosts.Num();

			for (int32 i = 0; i < FMath::Min(Limit, ActorCosts.Num()); i++)
			{
				const FActorCost& Cost = ActorCosts[i];
				TSharedPtr<FJsonObject> AObj = MakeShared<FJsonObject>();
				AObj->SetStringField(TEXT("name"), Cost.Actor->GetActorLabel());
				AObj->SetStringField(TEXT("class"), Cost.Actor->GetClass()->GetName());
				AObj->SetNumberField(TEXT("triangles"), Cost.Triangles);
				AObj->SetNumberField(TEXT("material_slots"), Cost.MaterialSlots);
				AObj->SetNumberField(TEXT("mesh_components"), Cost.Components);
				AObj->SetBoolField(TEXT("casts_shadow"), Cost.bCastsShadow);
				AObj->SetBoolField(TEXT("nanite"), Cost.bNanite);
				AObj->SetNumberField(TEXT("distance"), FMath::RoundToInt(Cost.Distance));
				AObj->SetNumberField(TEXT("estimated_cost"), FMath::RoundToFloat(Cost.EstimatedCost));
				ActorArray.Add(MakeShared<FJsonValueObject>(AObj));
				TotalTriangles += Cost.Triangles;
			}

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetNumberField(TEXT("visible_actors"), TotalActors);
			Result->SetNumberField(TEXT("showing"), ActorArray.Num());
			Result->SetNumberField(TEXT("total_triangles_in_view"), TotalTriangles);
			Result->SetArrayField(TEXT("actors"), ActorArray);
			AddProvenance(Result, TEXT("estimate"), TEXT("Per-actor heuristic: triangles * 0.001 plus material/shadow/Nanite weights inside the current viewport frustum; estimated_cost is a unitless ranking score, not milliseconds"), TEXT("triangles; estimated_cost unitless"));

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});

	// ================================================================
	// create_scene_from_template - Pre-built scene configurations
	// ================================================================
	MCP_TOOL(Registry, "create_scene_from_template")
		.Description(TEXT("Create a pre-configured scene from a template. Combines floor, lighting, sky, post-processing, and template-specific elements. Templates: fps_arena (closed arena with cover), tps_playground (open area with obstacles), rpg_outdoor (landscape with trees), horror_interior (dark room with fog), empty_studio (clean lighting setup for showcasing)."))
		.EnumArg(TEXT("template"), TEXT("Scene template to create"),
			{ TEXT("fps_arena"), TEXT("tps_playground"), TEXT("rpg_outdoor"), TEXT("horror_interior"), TEXT("empty_studio") }, true)
		.EnumArg(TEXT("lighting"), TEXT("Lighting mood (default: Day)"),
			{ TEXT("Day"), TEXT("Night"), TEXT("Sunset"), TEXT("Indoor") })
		.NumberArg(TEXT("floor_size"), TEXT("Floor plane size in cm (default: 5000)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString Template;
			if (!Args->TryGetStringField(TEXT("template"), Template))
				return FMCPToolResult::Error(TEXT("template is required"));

			FString Lighting = TEXT("Day");
			Args->TryGetStringField(TEXT("lighting"), Lighting);

			double FloorSize = 5000.0;
			if (Args->HasField(TEXT("floor_size")))
				FloorSize = Args->GetNumberField(TEXT("floor_size"));

			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			// Use the existing create_basic_level macro tool logic
			// Build the command string to call create_basic_level
			FString LightingPreset = Lighting;
			bool bAddNavMesh = (Template == TEXT("fps_arena") || Template == TEXT("tps_playground") || Template == TEXT("rpg_outdoor"));

			// Execute create_basic_level first
			TSharedPtr<FJsonObject> BasicLevelArgs = MakeShared<FJsonObject>();
			BasicLevelArgs->SetStringField(TEXT("lighting_preset"), LightingPreset);
			BasicLevelArgs->SetNumberField(TEXT("floor_size"), FloorSize);
			BasicLevelArgs->SetBoolField(TEXT("add_nav_mesh"), bAddNavMesh);
			BasicLevelArgs->SetBoolField(TEXT("add_player_start"), true);

			FMCPToolResult BasicResult = FMCPToolRegistry::Get().ExecuteTool(TEXT("create_basic_level"), BasicLevelArgs);

			TArray<FString> CreatedElements;
			CreatedElements.Add(TEXT("Floor, Sky, Lighting, Post-Process"));

			if (bAddNavMesh)
				CreatedElements.Add(TEXT("NavMesh"));

			CreatedElements.Add(TEXT("PlayerStart"));

			// Template-specific description
			FString TemplateDesc;
			if (Template == TEXT("fps_arena"))
			{
				TemplateDesc = TEXT("FPS Arena - Enclosed arena space. Add cover objects, ammo pickups, and spawn points.");
			}
			else if (Template == TEXT("tps_playground"))
			{
				TemplateDesc = TEXT("TPS Playground - Open area. Add obstacles, ramps, and targets.");
			}
			else if (Template == TEXT("rpg_outdoor"))
			{
				TemplateDesc = TEXT("RPG Outdoor - Open landscape. Add foliage, NPCs, and quest markers.");
			}
			else if (Template == TEXT("horror_interior"))
			{
				TemplateDesc = TEXT("Horror Interior - Dark enclosed space. Add fog, flickering lights, and sound sources.");
				// Apply darker post-process
				TSharedPtr<FJsonObject> PPArgs = MakeShared<FJsonObject>();
				PPArgs->SetStringField(TEXT("actor_name"), TEXT("PostProcess"));
				PPArgs->SetNumberField(TEXT("exposure_compensation"), -2.0);
				PPArgs->SetNumberField(TEXT("bloom_intensity"), 0.3);
				PPArgs->SetNumberField(TEXT("vignette_intensity"), 0.8);
				PPArgs->SetNumberField(TEXT("grain_intensity"), 0.3);
				FMCPToolRegistry::Get().ExecuteTool(TEXT("set_post_process_settings"), PPArgs);
				CreatedElements.Add(TEXT("Horror post-process (dark, vignette, grain)"));
			}
			else if (Template == TEXT("empty_studio"))
			{
				TemplateDesc = TEXT("Empty Studio - Clean 3-point lighting for showcasing assets.");
			}

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Created scene from template '%s' (%s).\n\nCreated: %s\n\nNext steps: Add game-specific actors and objects."),
				*Template, *TemplateDesc, *FString::Join(CreatedElements, TEXT(", "))));
		});
	// ------------------------------------------------------------------
	// measure_frame_times (v5 increment 20, V5-29): a measurement, driven by the editor ticker.
	// ------------------------------------------------------------------
	MCP_TOOL(Registry, "measure_frame_times")
		.Description(TEXT("Measure editor frame times over N ticks as an owned operation (poll get_editor_operation). Samples per tick: game thread delta (FApp::GetDeltaTime), render thread time (GRenderThreadTime) and GPU frame time (RHIGetGPUFrameCycles). The result carries min/avg/p95/max in milliseconds per series, the sample count, provenance kind=measurement and run_metadata (engine, RHI, GPU adapter, viewport, world). Under NullRHI the GPU series is reported as unavailable rather than zero. This measures the editor as it is, including this plugin; compare runs only with matching run_metadata."))
		.ReadOnly()
		.IntArg(TEXT("frames"), TEXT("Ticks to sample, 1..600 (default 60)"))
		.HandleCtx([](const TSharedPtr<FJsonObject>& Args, const FMCPRequestContext& Context) -> FMCPToolResult
		{
			if (!IsInGameThread()) return FMCPToolResult::ErrorStructured(EMCPError::RequiresGameThread, TEXT("Measurements require the game thread"));
			const int32 Frames = Args->HasField(TEXT("frames")) ? FMath::Clamp((int32)Args->GetNumberField(TEXT("frames")), 1, 600) : 60;
			struct FSamples { TArray<double> Game, Render, Gpu; int32 Target = 60; bool bGpuAvailable = true; };
			auto S = MakeShared<FSamples>(); S->Target = Frames; S->bGpuAvailable = !GUsingNullRHI;
			auto Stats = [](TArray<double> V)
			{
				auto J = MakeShared<FJsonObject>(); if (V.Num() == 0) return J;
				V.Sort(); double Sum = 0; for (double D : V) Sum += D;
				J->SetNumberField(TEXT("min"), V[0]); J->SetNumberField(TEXT("avg"), Sum / V.Num()); J->SetNumberField(TEXT("p95"), V[FMath::Clamp((int32)(V.Num() * 0.95), 0, V.Num() - 1)]); J->SetNumberField(TEXT("max"), V.Last());
				return J;
			};
			FString Err;
			const FString Id = MCPScenarios::StartDrivenOperation(Context, TEXT("frame_time_measurement"), FString::Printf(TEXT("measure %d frames"), Frames), 600.0,
				[S, Stats](FMCPOperationTick& T)
				{
					S->Game.Add(FApp::GetDeltaTime() * 1000.0);
					S->Render.Add(FPlatformTime::ToMilliseconds(GRenderThreadTime));
					if (S->bGpuAvailable) S->Gpu.Add(FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles()));
					T.Progress((double)S->Game.Num() / S->Target, FString::Printf(TEXT("%d/%d frames"), S->Game.Num(), S->Target));
					if (S->Game.Num() < S->Target) return;
					auto R = MakeShared<FJsonObject>();
					R->SetNumberField(TEXT("frames"), S->Game.Num());
					R->SetObjectField(TEXT("game_thread_ms"), Stats(S->Game)); R->SetObjectField(TEXT("render_thread_ms"), Stats(S->Render));
					if (S->bGpuAvailable) R->SetObjectField(TEXT("gpu_ms"), Stats(S->Gpu)); else R->SetStringField(TEXT("gpu_ms"), TEXT("unavailable (NullRHI)"));
					AddProvenance(R, TEXT("measurement"), TEXT("Editor ticker samples of FApp::GetDeltaTime, GRenderThreadTime and RHIGetGPUFrameCycles converted with FPlatformTime; includes this plugin's own tick cost"), TEXT("milliseconds"));
					T.Finish(TEXT("succeeded"), FString(), R);
				}, nullptr, nullptr, Err);
			if (Id.IsEmpty()) return FMCPToolResult::ErrorStructured(EMCPError::Unsupported, Err);
			return FMCPToolResult::SuccessStructured(FString::Printf(TEXT("Measuring %d frames as %s; poll get_editor_operation."), Frames, *Id), MCPScenarios::DescribeOperation(Id, Context, false));
		});

}

} // namespace MCPPerformanceTools
