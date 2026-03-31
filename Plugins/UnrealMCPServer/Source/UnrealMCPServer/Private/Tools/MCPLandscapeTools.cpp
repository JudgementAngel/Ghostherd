#include "Tools/MCPLandscapeTools.h"
#include "MCPToolRegistry.h"
#include "MCPProtocol.h"

#include "Landscape.h"
#include "LandscapeProxy.h"
#include "LandscapeInfo.h"
#include "LandscapeComponent.h"
#include "LandscapeEditLayer.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "ActorFactories/ActorFactory.h"
#include "Misc/Guid.h"

namespace MCPLandscapeTools
{

static UWorld* GetEditorWorld()
{
	if (GEditor)
	{
		return GEditor->GetEditorWorldContext().World();
	}
	return nullptr;
}

static ALandscapeProxy* FindLandscapeByLabel(UWorld* World, const FString& Label)
{
	for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
	{
		ALandscapeProxy* Proxy = *It;
		if (IsValid(Proxy) && Proxy->GetActorLabel() == Label)
		{
			return Proxy;
		}
	}
	return nullptr;
}

// Build a compact JSON object describing a single landscape proxy and return it as a string fragment.
static FString LandscapeProxyToJsonString(ALandscapeProxy* Proxy)
{
	if (!IsValid(Proxy))
	{
		return TEXT("null");
	}

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();

	// Basic identity
	Obj->SetStringField(TEXT("name"), Proxy->GetActorLabel());
	Obj->SetStringField(TEXT("class"), Proxy->GetClass()->GetName());
	Obj->SetStringField(TEXT("path"), Proxy->GetPathName());

	// Is this the root landscape actor or a streaming proxy?
	ALandscape* LandscapeActor = Cast<ALandscape>(Proxy);
	Obj->SetBoolField(TEXT("is_landscape_actor"), LandscapeActor != nullptr);

	// World location / scale
	FVector Location = Proxy->GetActorLocation();
	FVector Scale = Proxy->GetActorScale3D();
	FRotator Rotation = Proxy->GetActorRotation();

	TSharedPtr<FJsonObject> LocObj = MakeShared<FJsonObject>();
	LocObj->SetNumberField(TEXT("x"), Location.X);
	LocObj->SetNumberField(TEXT("y"), Location.Y);
	LocObj->SetNumberField(TEXT("z"), Location.Z);
	Obj->SetObjectField(TEXT("location"), LocObj);

	TSharedPtr<FJsonObject> RotObj = MakeShared<FJsonObject>();
	RotObj->SetNumberField(TEXT("pitch"), Rotation.Pitch);
	RotObj->SetNumberField(TEXT("yaw"), Rotation.Yaw);
	RotObj->SetNumberField(TEXT("roll"), Rotation.Roll);
	Obj->SetObjectField(TEXT("rotation"), RotObj);

	TSharedPtr<FJsonObject> ScaleObj = MakeShared<FJsonObject>();
	ScaleObj->SetNumberField(TEXT("x"), Scale.X);
	ScaleObj->SetNumberField(TEXT("y"), Scale.Y);
	ScaleObj->SetNumberField(TEXT("z"), Scale.Z);
	Obj->SetObjectField(TEXT("scale"), ScaleObj);

	// Component layout parameters
	Obj->SetNumberField(TEXT("component_size_quads"), Proxy->ComponentSizeQuads);
	Obj->SetNumberField(TEXT("subsection_size_quads"), Proxy->SubsectionSizeQuads);
	Obj->SetNumberField(TEXT("num_subsections"), Proxy->NumSubsections);

	// Component count
	int32 NumComponents = Proxy->LandscapeComponents.Num();
	Obj->SetNumberField(TEXT("component_count"), NumComponents);

	// Total vertex resolution: each component covers ComponentSizeQuads quads, plus the shared
	// border vertex on one edge, so the grid resolution per component = ComponentSizeQuads + 1.
	// For N components in each axis, the total quads = N * ComponentSizeQuads, total verts = N * ComponentSizeQuads + 1.
	// We compute component counts from GetBoundingRect when a LandscapeInfo is available.
	FIntRect BoundingRect = Proxy->GetBoundingRect();
	int32 QuadSpanX = BoundingRect.Width();
	int32 QuadSpanY = BoundingRect.Height();
	Obj->SetNumberField(TEXT("total_quads_x"), QuadSpanX);
	Obj->SetNumberField(TEXT("total_quads_y"), QuadSpanY);
	// Vertex resolution is quads + 1 in each axis
	Obj->SetNumberField(TEXT("total_verts_x"), QuadSpanX + 1);
	Obj->SetNumberField(TEXT("total_verts_y"), QuadSpanY + 1);

	// World-space bounds
	FVector BoundsOrigin, BoundsExtent;
	Proxy->GetActorBounds(false, BoundsOrigin, BoundsExtent);

	TSharedPtr<FJsonObject> BoundsObj = MakeShared<FJsonObject>();
	TSharedPtr<FJsonObject> BoundsMin = MakeShared<FJsonObject>();
	BoundsMin->SetNumberField(TEXT("x"), BoundsOrigin.X - BoundsExtent.X);
	BoundsMin->SetNumberField(TEXT("y"), BoundsOrigin.Y - BoundsExtent.Y);
	BoundsMin->SetNumberField(TEXT("z"), BoundsOrigin.Z - BoundsExtent.Z);
	TSharedPtr<FJsonObject> BoundsMax = MakeShared<FJsonObject>();
	BoundsMax->SetNumberField(TEXT("x"), BoundsOrigin.X + BoundsExtent.X);
	BoundsMax->SetNumberField(TEXT("y"), BoundsOrigin.Y + BoundsExtent.Y);
	BoundsMax->SetNumberField(TEXT("z"), BoundsOrigin.Z + BoundsExtent.Z);
	BoundsObj->SetObjectField(TEXT("min"), BoundsMin);
	BoundsObj->SetObjectField(TEXT("max"), BoundsMax);
	Obj->SetObjectField(TEXT("world_bounds"), BoundsObj);

	// Landscape material
	if (IsValid(Proxy->LandscapeMaterial))
	{
		Obj->SetStringField(TEXT("material"), Proxy->LandscapeMaterial->GetPathName());
	}
	else
	{
		Obj->SetStringField(TEXT("material"), TEXT("None"));
	}

	// Paint layers from LandscapeInfo (editor-only)
	ULandscapeInfo* LandscapeInfo = Proxy->GetLandscapeInfo();
	if (IsValid(LandscapeInfo))
	{
		TArray<TSharedPtr<FJsonValue>> LayerArray;

#if WITH_EDITORONLY_DATA
		for (const FLandscapeInfoLayerSettings& LayerSettings : LandscapeInfo->Layers)
		{
			FName LayerName = LayerSettings.GetLayerName();
			if (!LayerName.IsNone())
			{
				TSharedPtr<FJsonObject> LayerObj = MakeShared<FJsonObject>();
				LayerObj->SetStringField(TEXT("name"), LayerName.ToString());
				if (IsValid(LayerSettings.LayerInfoObj))
				{
					LayerObj->SetStringField(TEXT("layer_info"), LayerSettings.LayerInfoObj->GetPathName());
				}
				else
				{
					LayerObj->SetStringField(TEXT("layer_info"), TEXT("None"));
				}
				LayerArray.Add(MakeShared<FJsonValueObject>(LayerObj));
			}
		}
#endif // WITH_EDITORONLY_DATA

		Obj->SetArrayField(TEXT("paint_layers"), LayerArray);
		Obj->SetNumberField(TEXT("paint_layer_count"), LayerArray.Num());

		// Report the full landscape extent (across all proxies if streaming)
		FIntRect FullExtent;
		if (LandscapeInfo->GetLandscapeExtent(FullExtent))
		{
			TSharedPtr<FJsonObject> ExtentObj = MakeShared<FJsonObject>();
			ExtentObj->SetNumberField(TEXT("min_x"), FullExtent.Min.X);
			ExtentObj->SetNumberField(TEXT("min_y"), FullExtent.Min.Y);
			ExtentObj->SetNumberField(TEXT("max_x"), FullExtent.Max.X);
			ExtentObj->SetNumberField(TEXT("max_y"), FullExtent.Max.Y);
			ExtentObj->SetNumberField(TEXT("width"), FullExtent.Width());
			ExtentObj->SetNumberField(TEXT("height"), FullExtent.Height());
			Obj->SetObjectField(TEXT("full_landscape_extent_quads"), ExtentObj);
		}

		// Edit layer names (ALandscape only, UE5.7 style)
		if (LandscapeActor != nullptr)
		{
			TArray<TSharedPtr<FJsonValue>> EditLayerArray;
			TArrayView<const FLandscapeLayer> EditLayers = LandscapeActor->GetLayersConst();
			for (const FLandscapeLayer& EditLayer : EditLayers)
			{
				// Access the edit layer object via TObjectPtr::Get() and read its FName
				UObject* EditLayerObj = EditLayer.EditLayer.Get();
				if (EditLayerObj)
				{
					EditLayerArray.Add(MakeShared<FJsonValueString>(EditLayerObj->GetFName().ToString()));
				}
			}
			Obj->SetArrayField(TEXT("edit_layers"), EditLayerArray);
			Obj->SetNumberField(TEXT("edit_layer_count"), EditLayerArray.Num());
		}
	}
	else
	{
		Obj->SetArrayField(TEXT("paint_layers"), TArray<TSharedPtr<FJsonValue>>());
		Obj->SetNumberField(TEXT("paint_layer_count"), 0);
	}

	return JsonToString(Obj);
}

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// get_landscape_info - Read-only info about all landscapes in the level
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("actor_name"), TEXT("Optional: filter to a specific landscape actor by label. If empty, all landscapes are returned."));

		FMCPToolDefinition Def;
		Def.Name = TEXT("get_landscape_info");
		Def.Description = TEXT(
			"Get detailed information about landscape actors in the current level. "
			"Reports name, class, component counts, quad/vertex resolution, world bounds, "
			"scale, material, paint layers, and edit layers for each ALandscape and ALandscapeStreamingProxy. "
			"Useful for understanding existing terrain before making modifications."
		);
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

			FString NameFilter;
			Args->TryGetStringField(TEXT("actor_name"), NameFilter);

			TArray<FString> LandscapeJsons;
			int32 TotalFound = 0;

			for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
			{
				ALandscapeProxy* Proxy = *It;
				if (!IsValid(Proxy))
				{
					continue;
				}

				if (!NameFilter.IsEmpty() && Proxy->GetActorLabel() != NameFilter)
				{
					continue;
				}

				TotalFound++;
				LandscapeJsons.Add(LandscapeProxyToJsonString(Proxy));
			}

			if (TotalFound == 0)
			{
				if (!NameFilter.IsEmpty())
				{
					return FMCPToolResult::Error(FString::Printf(
						TEXT("No landscape actor found with label '%s'"), *NameFilter));
				}
				return FMCPToolResult::Success(TEXT("No landscape actors found in the current level. Use create_landscape to add one."));
			}

			FString Result = FString::Printf(
				TEXT("Found %d landscape actor(s):\n[%s]"),
				TotalFound,
				*FString::Join(LandscapeJsons, TEXT(",\n")));

			return FMCPToolResult::Success(Result);
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// create_landscape - Spawn a new landscape actor
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("x"), TEXT("World X position of the landscape origin (default: 0)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("y"), TEXT("World Y position of the landscape origin (default: 0)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("z"), TEXT("World Z position of the landscape origin (default: 0)"));
		FMCPSchemaBuilder::AddInteger(Schema, TEXT("num_components_x"),
			TEXT("Number of landscape components along X axis (default: 8). "
			     "Total quad width = num_components_x * sections_per_component * quads_per_section."));
		FMCPSchemaBuilder::AddInteger(Schema, TEXT("num_components_y"),
			TEXT("Number of landscape components along Y axis (default: 8). "
			     "Total quad height = num_components_y * sections_per_component * quads_per_section."));
		FMCPSchemaBuilder::AddEnum(Schema, TEXT("quads_per_section"),
			TEXT("Quads per subsection. Must be one of the standard UE values: 7, 15, 31, 63, 127, 255. "
			     "Higher values = higher per-component resolution. Default: 63."),
			{ TEXT("7"), TEXT("15"), TEXT("31"), TEXT("63"), TEXT("127"), TEXT("255") });
		FMCPSchemaBuilder::AddEnum(Schema, TEXT("sections_per_component"),
			TEXT("Number of subsections per component (1 or 2). Default: 1. "
			     "Using 2 doubles the component's quad count and LOD flexibility."),
			{ TEXT("1"), TEXT("2") });
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("scale_x"),
			TEXT("Landscape scale on X axis in cm per quad (default: 100). "
			     "At scale 100 each quad = 1m in world space."));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("scale_y"),
			TEXT("Landscape scale on Y axis in cm per quad (default: 100)."));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("scale_z"),
			TEXT("Landscape Z scale controlling height range (default: 100). "
			     "Actual height range in cm = +/- 256 * scale_z."));
		FMCPSchemaBuilder::AddString(Schema, TEXT("label"),
			TEXT("Actor label shown in the World Outliner (default: 'Landscape')."));

		FMCPToolDefinition Def;
		Def.Name = TEXT("create_landscape");
		Def.Description = TEXT(
			"Create a new flat landscape actor in the current level using UE's standard import pipeline. "
			"The landscape is initialised with a flat (mid-grey) heightmap. "
			"Parameters control the component grid, subsection size, and world-space scale. "
			"Typical presets:\n"
			"  Small  (1 km^2):  8x8  components, 1 section, 63 quads, scale 100\n"
			"  Medium (4 km^2): 16x16 components, 1 section, 63 quads, scale 100\n"
			"  Large  (8 km^2): 16x16 components, 2 sections, 127 quads, scale 100\n"
			"After creation use get_landscape_info to confirm the result."
		);
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World)
			{
				return FMCPToolResult::Error(TEXT("No editor world available"));
			}

			// --- Parse position ---
			FVector Location(
				Args->HasField(TEXT("x")) ? Args->GetNumberField(TEXT("x")) : 0.0,
				Args->HasField(TEXT("y")) ? Args->GetNumberField(TEXT("y")) : 0.0,
				Args->HasField(TEXT("z")) ? Args->GetNumberField(TEXT("z")) : 0.0
			);

			// --- Parse component layout ---
			int32 NumComponentsX = 8;
			int32 NumComponentsY = 8;
			if (Args->HasField(TEXT("num_components_x")))
			{
				NumComponentsX = FMath::Clamp((int32)Args->GetNumberField(TEXT("num_components_x")), 1, 64);
			}
			if (Args->HasField(TEXT("num_components_y")))
			{
				NumComponentsY = FMath::Clamp((int32)Args->GetNumberField(TEXT("num_components_y")), 1, 64);
			}

			// --- Parse quads per section ---
			// Valid UE5 values: 7, 15, 31, 63, 127, 255
			static const int32 ValidQuadSizes[] = { 7, 15, 31, 63, 127, 255 };
			int32 QuadsPerSection = 63;
			if (Args->HasField(TEXT("quads_per_section")))
			{
				FString QuadsStr;
				Args->TryGetStringField(TEXT("quads_per_section"), QuadsStr);
				int32 ParsedQuads = FCString::Atoi(*QuadsStr);
				bool bValid = false;
				for (int32 ValidSize : ValidQuadSizes)
				{
					if (ParsedQuads == ValidSize)
					{
						QuadsPerSection = ParsedQuads;
						bValid = true;
						break;
					}
				}
				if (!bValid)
				{
					return FMCPToolResult::Error(FString::Printf(
						TEXT("Invalid quads_per_section value '%s'. Must be one of: 7, 15, 31, 63, 127, 255."),
						*QuadsStr));
				}
			}

			// --- Parse sections per component ---
			int32 SectionsPerComponent = 1;
			if (Args->HasField(TEXT("sections_per_component")))
			{
				FString SectionsStr;
				Args->TryGetStringField(TEXT("sections_per_component"), SectionsStr);
				int32 ParsedSections = FCString::Atoi(*SectionsStr);
				if (ParsedSections == 1 || ParsedSections == 2)
				{
					SectionsPerComponent = ParsedSections;
				}
				else
				{
					return FMCPToolResult::Error(TEXT("sections_per_component must be 1 or 2."));
				}
			}

			// --- Parse scale ---
			FVector Scale(
				Args->HasField(TEXT("scale_x")) ? Args->GetNumberField(TEXT("scale_x")) : 100.0,
				Args->HasField(TEXT("scale_y")) ? Args->GetNumberField(TEXT("scale_y")) : 100.0,
				Args->HasField(TEXT("scale_z")) ? Args->GetNumberField(TEXT("scale_z")) : 100.0
			);

			// Clamp scale to sensible range to avoid degenerate landscapes
			Scale.X = FMath::Clamp(Scale.X, 1.0, 100000.0);
			Scale.Y = FMath::Clamp(Scale.Y, 1.0, 100000.0);
			Scale.Z = FMath::Clamp(Scale.Z, 1.0, 100000.0);

			// --- Label ---
			FString Label = TEXT("Landscape");
			Args->TryGetStringField(TEXT("label"), Label);
			if (Label.IsEmpty())
			{
				Label = TEXT("Landscape");
			}

			// --- Compute total resolution ---
			// Total quads in each axis = numComponents * sectionsPerComponent * quadsPerSection
			// Total verts = totalQuads + 1
			const int32 QuadsPerComponent = SectionsPerComponent * QuadsPerSection;
			const int32 SizeX = NumComponentsX * QuadsPerComponent + 1; // vertex count X
			const int32 SizeY = NumComponentsY * QuadsPerComponent + 1; // vertex count Y
			const int32 TotalQuadsX = SizeX - 1;
			const int32 TotalQuadsY = SizeY - 1;

			// Safety check: very large landscapes can run out of memory
			const int32 MaxVerts = 4097 * 4097; // ~16M, safe upper bound
			if (SizeX * SizeY > MaxVerts)
			{
				return FMCPToolResult::Error(FString::Printf(
					TEXT("Requested landscape is too large (%d x %d vertices = %d total). "
					     "Reduce num_components or quads_per_section. Maximum safe total is ~16 million vertices."),
					SizeX, SizeY, SizeX * SizeY));
			}

			// --- Build a flat heightmap ---
			// UE stores heights as uint16. Mid value = 32768 = flat ground.
			const int32 HeightmapSamples = SizeX * SizeY;
			TArray<uint16> HeightData;
			HeightData.SetNumUninitialized(HeightmapSamples);
			for (int32 i = 0; i < HeightmapSamples; i++)
			{
				HeightData[i] = 32768; // mid-grey = flat
			}

			// --- Offset origin so the landscape is centred on Location ---
			// The Import() function treats (0,0) as the component origin.
			// We shift Location by half the landscape extent so the user-supplied
			// position is the world-space centre of the terrain.
			FVector Offset = FTransform(FRotator::ZeroRotator, FVector::ZeroVector, Scale)
				.TransformVector(FVector(
					-(float)TotalQuadsX * 0.5f,
					-(float)TotalQuadsY * 0.5f,
					0.0f));
			FVector SpawnLocation = Location + Offset;

			// --- Spawn the landscape actor ---
			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Create Landscape")));

			ALandscape* NewLandscape = World->SpawnActor<ALandscape>(SpawnLocation, FRotator::ZeroRotator);
			if (!IsValid(NewLandscape))
			{
				GEditor->EndTransaction();
				return FMCPToolResult::Error(TEXT("Failed to spawn ALandscape actor. Check the Output Log for details."));
			}

			NewLandscape->SetActorRelativeScale3D(Scale);

			// StaticLightingLOD: auto-compute to avoid lightmass crashes on very large landscapes
			NewLandscape->StaticLightingLOD = FMath::DivideAndRoundUp(
				FMath::CeilLogTwo((SizeX * SizeY) / (2048 * 2048) + 1), (uint32)2);

			// Build the import data maps (one entry per layer Guid; we only need the base layer)
			TMap<FGuid, TArray<uint16>> HeightDataPerLayers;
			HeightDataPerLayers.Add(FGuid(), MoveTemp(HeightData));

			TMap<FGuid, TArray<FLandscapeImportLayerInfo>> MaterialLayerDataPerLayers;
			MaterialLayerDataPerLayers.Add(FGuid(), TArray<FLandscapeImportLayerInfo>());

			// Import: coordinate space is (MinX, MinY) to (MaxX, MaxY) inclusive, i.e. max = vertCount - 1
			NewLandscape->Import(
				FGuid::NewGuid(),
				0, 0,
				TotalQuadsX, TotalQuadsY,
				SectionsPerComponent,
				QuadsPerSection,
				HeightDataPerLayers,
				TEXT(""),                                    // no reimport file
				MaterialLayerDataPerLayers,
				ELandscapeImportAlphamapType::Additive,
				TArrayView<const FLandscapeLayer>()          // no legacy layers
			);

			// Fetch LandscapeInfo and finalise
			ULandscapeInfo* LandscapeInfo = NewLandscape->GetLandscapeInfo();
			if (IsValid(LandscapeInfo))
			{
				LandscapeInfo->UpdateLayerInfoMap(NewLandscape);
			}

			// Apply label
			NewLandscape->SetActorLabel(Label);

			GEditor->EndTransaction();

			// --- Build a concise summary for the response ---
			FVector FinalLocation = NewLandscape->GetActorLocation();
			float WorldSizeX = (float)TotalQuadsX * Scale.X * 0.01f; // quads * cm/quad * m/cm = metres
			float WorldSizeY = (float)TotalQuadsY * Scale.Y * 0.01f;
			float HeightRangeCm = 256.0f * Scale.Z;

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Created landscape '%s':\n"
				     "  Components:       %dx%d (%d total)\n"
				     "  Sections/Comp:    %d\n"
				     "  Quads/Section:    %d\n"
				     "  Total quads:      %dx%d\n"
				     "  Total vertices:   %dx%d (%d total)\n"
				     "  Scale (cm/quad):  %.1f, %.1f, %.1f\n"
				     "  World size:       %.0f m x %.0f m\n"
				     "  Height range:     +/- %.0f cm (%.0f m) from base Z\n"
				     "  Spawn location:   (%.1f, %.1f, %.1f)\n"
				     "Use get_landscape_info for full details."),
				*NewLandscape->GetActorLabel(),
				NumComponentsX, NumComponentsY, NumComponentsX * NumComponentsY,
				SectionsPerComponent,
				QuadsPerSection,
				TotalQuadsX, TotalQuadsY,
				SizeX, SizeY, SizeX * SizeY,
				Scale.X, Scale.Y, Scale.Z,
				WorldSizeX, WorldSizeY,
				HeightRangeCm, HeightRangeCm * 0.01f,
				FinalLocation.X, FinalLocation.Y, FinalLocation.Z
			));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// set_landscape_material - Assign a material to a landscape
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("actor_name"), TEXT("Label of the landscape actor"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("material_path"), TEXT("Content path of the material to assign (e.g., '/Game/Materials/M_Landscape')"), true);

		FMCPToolDefinition Def;
		Def.Name = TEXT("set_landscape_material");
		Def.Description = TEXT("Assign a material to a landscape actor's LandscapeMaterial slot. The material should be a landscape-compatible material with layer blend nodes for paint layers.");
		Def.InputSchema = Schema;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString ActorName, MaterialPath;
			if (!Args->TryGetStringField(TEXT("actor_name"), ActorName)) return FMCPToolResult::Error(TEXT("actor_name required"));
			if (!Args->TryGetStringField(TEXT("material_path"), MaterialPath)) return FMCPToolResult::Error(TEXT("material_path required"));

			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			ALandscapeProxy* Proxy = FindLandscapeByLabel(World, ActorName);
			if (!Proxy) return FMCPToolResult::Error(FString::Printf(TEXT("Landscape not found: %s"), *ActorName));

			UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *MaterialPath);
			if (!Material) return FMCPToolResult::Error(FString::Printf(TEXT("Material not found: %s"), *MaterialPath));

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Set Landscape Material")));
			Proxy->Modify();
			Proxy->LandscapeMaterial = Material;
			Proxy->UpdateAllComponentMaterialInstances();
			GEditor->EndTransaction();

			return FMCPToolResult::Success(FString::Printf(TEXT("Assigned material '%s' to landscape '%s'"),
				*Material->GetName(), *ActorName));
		});
		Registry.RegisterTool(Def);
	}

} // void RegisterAll

} // namespace MCPLandscapeTools
