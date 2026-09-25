// Copyright StraySpark Studio 2026. All Rights Reserved.
//
// Phase D.7 — Chaos / Destruction tool family.
//
// The Chaos APIs (UGeometryCollection, UFractureToolContext, UChaosClothAsset)
// live in plugin modules `GeometryCollectionEngine`, `ChaosCloth`,
// `FractureEngine`, `Chaos`. Their surface has shifted between 5.4 → 5.7 and
// is expected to keep moving.
//
// Strategy here matches D.5 / D.6: register stable schemas; runtime-side
// tools that don't require a hard module dep (`chaos_apply_force` against
// any actor with a primitive component, list helpers via reflection) are
// implemented; everything else returns `EMCPError::Unsupported` with a hint
// pointing at the required plugins.

#include "Tools/MCPChaosTools.h"
#include "Common/MCPActorResolver.h"

#include "MCPToolRegistry.h"
#include "MCPToolBuilder.h"
#include "MCPProtocol.h"
#include "MCPValidate.h"

#include "Editor.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Components/PrimitiveComponent.h"
#include "Materials/MaterialInterface.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"

// v4 Phase 3 — geometry-collection creation + Voronoi fracture, for real.
#include "GeometryCollection/GeometryCollectionObject.h"
#include "GeometryCollection/GeometryCollectionEngineConversion.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/Facades/CollectionTransformSelectionFacade.h"
#include "GeometryCollection/Facades/CollectionBoundsFacade.h"
#include "Dataflow/DataflowSelection.h"
#include "FractureEngineFracturing.h"

// v4.5 (5.8) — Chaos field actors (FieldSystemEngine) + cloth asset (ChaosClothAssetEngine).
#include "Field/FieldSystemActor.h"
#include "Field/FieldSystemComponent.h"
#include "Field/FieldSystemObjects.h"
#include "Field/FieldSystemTypes.h"
#include "ChaosClothAsset/ClothAsset.h"
#include "ChaosClothAsset/ClothAssetFactory.h"
#include "Engine/SkeletalMesh.h"
#include "Common/MCPAssetCreate.h"

namespace MCPChaosTools
{

// v4.5: all five chaos tools are now implemented (geometry collection, fracture,
// add_field, create_cloth_asset, apply_force) — the old NotYet/PluginHint stubs
// were removed.

static AActor* FindActorByLabel(UWorld* World, const FString& Label)
{
	// v4 Phase 1: cached resolver (O(1) amortized) replaces the per-call actor scan.
	return MCPCommon::FindActorByLabel(World, Label);
}

void RegisterAll(FMCPToolRegistry& Registry)
{
	MCP_TOOL(Registry, "chaos_create_geometry_collection")
		.Description(TEXT(
			"Create a UGeometryCollection asset from a list of source static meshes. "
			"Wraps GeometryCollectionEngine."))
		.StringArg(TEXT("asset_path"), TEXT("Destination /Game path for the new collection."), true)
		.StringArrayArg(TEXT("source_meshes"), TEXT("Static mesh asset paths to embed."), true)
		.SupportsDryRun()
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			// v4 Phase 3: implemented — mirrors UGeometryCollectionFactory's flow
			// (AppendStaticMesh per source, InitializeMaterials, invalidate).
			FString AssetPath;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("asset_path"), AssetPath));
			BAIL_IF_INVALID(FMCPValidate::PackagePath(AssetPath));
			BAIL_IF_INVALID(FMCPValidate::AssetDoesNotExist(AssetPath));
			TArray<FString> SourcePaths;
			BAIL_IF_INVALID(FMCPValidate::RequiredStringArray(Args, TEXT("source_meshes"), SourcePaths));

			// Resolve all sources before creating anything (all-or-nothing).
			TArray<UStaticMesh*> Sources;
			for (const FString& Path : SourcePaths)
			{
				UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *Path);
				if (!Mesh)
				{
					return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
						FString::Printf(TEXT("Static mesh not found: %s. Nothing was created."), *Path));
				}
				Sources.Add(Mesh);
			}

			UPackage* Package = CreatePackage(*AssetPath);
			const FString AssetName = FPackageName::GetLongPackageAssetName(AssetPath);
			UGeometryCollection* Collection = NewObject<UGeometryCollection>(
				Package, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional);

			int32 Appended = 0;
			for (UStaticMesh* Mesh : Sources)
			{
				TArray<UMaterialInterface*> Materials;
				for (const FStaticMaterial& StaticMat : Mesh->GetStaticMaterials())
				{
					Materials.Add(StaticMat.MaterialInterface);
				}
				FGeometryCollectionEngineConversion::AppendStaticMesh(
					Mesh, Materials, FTransform::Identity, Collection, /*ReindexMaterials*/ false);
				Appended++;
			}

			Collection->InitializeMaterials();
			Collection->InvalidateCollection();
			Collection->RebuildRenderData();

			FAssetRegistryModule::AssetCreated(Collection);
			Package->MarkPackageDirty();
			// Journal the creation so run_tool_script can report truthfully that this
			// asset survives a rollback (UE package creation is not transactional).
			MCPCommon::NoteAssetCreated(AssetPath);

			TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
			Out->SetStringField(TEXT("asset"), Collection->GetPathName());
			Out->SetNumberField(TEXT("source_meshes"), Appended);
			const int32 NumTransforms = Collection->GetGeometryCollection().IsValid()
				? Collection->GetGeometryCollection()->Transform.Num() : 0;
			Out->SetNumberField(TEXT("transforms"), NumTransforms);
			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Created geometry collection '%s' from %d mesh(es) (%d transform(s)). Fracture it with chaos_fracture."),
					*AssetPath, Appended, NumTransforms),
				Out);
		});

	MCP_TOOL(Registry, "chaos_fracture")
		.Description(TEXT(
			"Fracture a geometry collection using one of the standard methods. Wraps FractureEngine."))
		.Destructive()
		.StringArg(TEXT("collection_path"), TEXT("Geometry collection asset path."), true)
		.EnumArg(TEXT("method"), TEXT("Fracture method."),
			{ TEXT("uniform"), TEXT("cluster"), TEXT("voronoi"), TEXT("planar") }, true)
		.IntArg(TEXT("num_pieces"), TEXT("Target number of pieces (when applicable)."))
		.IntArg(TEXT("seed"), TEXT("Random seed for reproducibility."))
		.SupportsDryRun()
		.LongRunning()
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			// v4 Phase 3: implemented via FFractureEngineFracturing (mirrors the
			// Dataflow VoronoiFracture node: select-all transforms, scatter sites
			// in the collection bounds, fracture). 'uniform'/'cluster'/'voronoi'
			// map to Voronoi with different site patterns; 'planar' uses PlaneCutter.
			FString CollectionPath, Method;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("collection_path"), CollectionPath));
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("method"), Method));
			BAIL_IF_INVALID(FMCPValidate::OneOf(Method,
				{ TEXT("uniform"), TEXT("cluster"), TEXT("voronoi"), TEXT("planar") }, TEXT("method")));

			int32 NumPieces = 10;
			if (Args->HasField(TEXT("num_pieces")))
			{
				NumPieces = FMath::Clamp((int32)Args->GetNumberField(TEXT("num_pieces")), 2, 5000);
			}
			int32 Seed = 0;
			if (Args->HasField(TEXT("seed")))
			{
				Seed = (int32)Args->GetNumberField(TEXT("seed"));
			}

			UGeometryCollection* Asset = LoadObject<UGeometryCollection>(nullptr, *CollectionPath);
			if (!Asset)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
					FString::Printf(TEXT("Geometry collection not found: %s"), *CollectionPath),
					TEXT("Create one first with chaos_create_geometry_collection."));
			}
			TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> Geo = Asset->GetGeometryCollection();
			if (!Geo.IsValid())
			{
				return FMCPToolResult::ErrorStructured(EMCPError::Internal,
					TEXT("Geometry collection asset has no collection data."));
			}

			const int32 TransformsBefore = Geo->Transform.Num();

			// Select every transform (fracture the whole collection).
			GeometryCollection::Facades::FCollectionTransformSelectionFacade SelectionFacade(*Geo);
			FDataflowTransformSelection Selection;
			Selection.InitializeFromCollection(*Geo, false);
			Selection.SetFromArray(SelectionFacade.SelectAll());
			if (!Selection.AnySelected())
			{
				return FMCPToolResult::ErrorStructured(EMCPError::Internal,
					TEXT("Collection has no selectable transforms."));
			}

			// Bounds for site scattering / plane generation.
			GeometryCollection::Facades::FBoundsFacade BoundsFacade(*Geo);
			const FBox Bounds = BoundsFacade.GetBoundingBoxInCollectionSpace();
			if (!Bounds.IsValid)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::Internal,
					TEXT("Could not compute collection bounds."));
			}

			// Noise/quality defaults mirror the Dataflow node defaults.
			constexpr float ChanceToFracture = 1.0f;
			constexpr bool  bSplitIslands = true;
			constexpr float Grout = 0.0f;
			constexpr float Amplitude = 0.0f;
			constexpr float Frequency = 0.1f;
			constexpr float Persistence = 0.5f;
			constexpr float Lacunarity = 2.0f;
			constexpr int32 OctaveNumber = 4;
			constexpr float PointSpacing = 10.0f;
			constexpr bool  bAddSamplesForCollision = false;
			constexpr float CollisionSampleSpacing = 50.0f;

			int32 ResultIndex = INDEX_NONE;
			if (Method == TEXT("planar"))
			{
				const int32 NumPlanes = FMath::Max(1, NumPieces - 1);
				ResultIndex = FFractureEngineFracturing::PlaneCutter(*Geo, Selection, Bounds,
					FTransform::Identity, NumPlanes, Seed, ChanceToFracture, bSplitIslands, Grout,
					Amplitude, Frequency, Persistence, Lacunarity, OctaveNumber, PointSpacing,
					bAddSamplesForCollision, CollisionSampleSpacing);
			}
			else
			{
				// Site scatter: uniform/voronoi = uniform random; cluster = grouped
				// clumps (sites concentrated around a few cluster centers).
				FRandomStream Rand(Seed);
				TArray<FVector> Sites;
				if (Method == TEXT("cluster"))
				{
					const int32 NumClusters = FMath::Clamp(NumPieces / 5, 1, 50);
					const FVector Extent = Bounds.GetExtent();
					for (int32 c = 0; c < NumClusters; ++c)
					{
						const FVector Center = Bounds.Min + FVector(
							Rand.FRand() * 2 * Extent.X, Rand.FRand() * 2 * Extent.Y, Rand.FRand() * 2 * Extent.Z);
						const int32 PerCluster = FMath::Max(1, NumPieces / NumClusters);
						const double Radius = Extent.GetMin() * 0.25;
						for (int32 i = 0; i < PerCluster; ++i)
						{
							Sites.Add(Center + Rand.GetUnitVector() * Rand.FRand() * Radius);
						}
					}
				}
				else
				{
					for (int32 i = 0; i < NumPieces; ++i)
					{
						Sites.Add(FVector(
							Rand.FRandRange(Bounds.Min.X, Bounds.Max.X),
							Rand.FRandRange(Bounds.Min.Y, Bounds.Max.Y),
							Rand.FRandRange(Bounds.Min.Z, Bounds.Max.Z)));
					}
				}

				ResultIndex = FFractureEngineFracturing::VoronoiFracture(*Geo, Selection, Sites,
					FTransform::Identity, Seed, ChanceToFracture, bSplitIslands, Grout,
					Amplitude, Frequency, Persistence, Lacunarity, OctaveNumber, PointSpacing,
					bAddSamplesForCollision, CollisionSampleSpacing);
			}

			const int32 TransformsAfter = Geo->Transform.Num();
			if (ResultIndex == INDEX_NONE && TransformsAfter == TransformsBefore)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::Internal,
					TEXT("Fracture produced no new pieces."),
					TEXT("Check the collection has geometry; try more pieces or a different seed."));
			}

			Asset->InvalidateCollection();
			Asset->RebuildRenderData();
			Asset->MarkPackageDirty();

			TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
			Out->SetStringField(TEXT("asset"), Asset->GetPathName());
			Out->SetStringField(TEXT("method"), Method);
			Out->SetNumberField(TEXT("transforms_before"), TransformsBefore);
			Out->SetNumberField(TEXT("transforms_after"), TransformsAfter);
			Out->SetNumberField(TEXT("new_pieces"), TransformsAfter - TransformsBefore);
			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Fractured '%s' (%s): %d -> %d transforms (%d new pieces)."),
					*CollectionPath, *Method, TransformsBefore, TransformsAfter, TransformsAfter - TransformsBefore),
				Out);
		});

	MCP_TOOL(Registry, "chaos_add_field")
		.Description(TEXT(
			"Spawn a Chaos Field System actor with a persistent construction field that influences nearby "
			"simulating Chaos bodies (e.g. geometry collections) at runtime. 'radial_falloff' applies an "
			"external cluster strain inside 'radius' (breaks fractured pieces); 'uniform_vector' applies a "
			"constant linear force along +Z. The field acts when physics runs (PIE)."))
		.StringArg(TEXT("name"), TEXT("Editor label for the new field actor."), true)
		.EnumArg(TEXT("field_type"), TEXT("Field type."),
			{ TEXT("radial_falloff"), TEXT("uniform_vector") }, true)
		.NumberArg(TEXT("magnitude"), TEXT("Magnitude / strength (default 1000000 for strain, 10000 for force)."))
		.NumberArg(TEXT("radius"), TEXT("Sphere radius for radial_falloff (default 200)."))
		.NumberArg(TEXT("location_x"), TEXT("Location X."))
		.NumberArg(TEXT("location_y"), TEXT("Location Y."))
		.NumberArg(TEXT("location_z"), TEXT("Location Z."))
		.SupportsDryRun()
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			FString Name, FieldType;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("name"), Name));
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("field_type"), FieldType));
			BAIL_IF_INVALID(FMCPValidate::OneOf(FieldType,
				{ TEXT("radial_falloff"), TEXT("uniform_vector") }, TEXT("field_type")));

			const FVector Location(
				Args->HasField(TEXT("location_x")) ? Args->GetNumberField(TEXT("location_x")) : 0.0,
				Args->HasField(TEXT("location_y")) ? Args->GetNumberField(TEXT("location_y")) : 0.0,
				Args->HasField(TEXT("location_z")) ? Args->GetNumberField(TEXT("location_z")) : 0.0);
			const double Radius = Args->HasField(TEXT("radius")) ? Args->GetNumberField(TEXT("radius")) : 200.0;
			const bool bRadial = FieldType == TEXT("radial_falloff");
			const double Magnitude = Args->HasField(TEXT("magnitude"))
				? Args->GetNumberField(TEXT("magnitude"))
				: (bRadial ? 1000000.0 : 10000.0);

			UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
			if (!World) return FMCPToolResult::ErrorStructured(EMCPError::Internal, TEXT("No editor world"));

			FActorSpawnParameters SpawnParams;
			AFieldSystemActor* FieldActor = World->SpawnActor<AFieldSystemActor>(Location, FRotator::ZeroRotator, SpawnParams);
			if (!FieldActor) return FMCPToolResult::ErrorStructured(EMCPError::Internal,
				TEXT("Failed to spawn AFieldSystemActor"));
			FieldActor->SetActorLabel(Name);

			UFieldSystemComponent* FieldComp = FieldActor->GetFieldSystemComponent();
			if (!FieldComp)
			{
				World->DestroyActor(FieldActor);
				return FMCPToolResult::ErrorStructured(EMCPError::Internal,
					TEXT("Spawned field actor has no FieldSystemComponent"));
			}

			if (bRadial)
			{
				URadialFalloff* Field = NewObject<URadialFalloff>(FieldComp);
				Field->SetRadialFalloff((float)Magnitude, /*MinRange*/ 0.f, /*MaxRange*/ 1.f,
					/*Default*/ 0.f, (float)Radius, Location, EFieldFalloffType::Field_Falloff_Linear);
				FieldComp->AddFieldCommand(/*Enabled*/ true, EFieldPhysicsType::Field_ExternalClusterStrain,
					/*MetaData*/ nullptr, Field);
			}
			else
			{
				UUniformVector* Field = NewObject<UUniformVector>(FieldComp);
				Field->SetUniformVector((float)Magnitude, FVector::UpVector);
				FieldComp->AddFieldCommand(/*Enabled*/ true, EFieldPhysicsType::Field_LinearForce,
					/*MetaData*/ nullptr, Field);
			}

			TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetStringField(TEXT("actor"), FieldActor->GetActorLabel());
			R->SetStringField(TEXT("field_type"), FieldType);
			R->SetNumberField(TEXT("magnitude"), Magnitude);
			if (bRadial) R->SetNumberField(TEXT("radius"), Radius);
			R->SetStringField(TEXT("physics_target"),
				bRadial ? TEXT("ExternalClusterStrain") : TEXT("LinearForce"));
			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Spawned field '%s' (%s). It influences Chaos bodies when physics runs (PIE)."),
					*Name, *FieldType), R);
		});

	MCP_TOOL(Registry, "chaos_create_cloth_asset")
		.Description(TEXT(
			"Create a Chaos Cloth Asset (UChaosClothAsset) via the engine's scripted factory helper. "
			"Validates the source skeletal mesh and records it on the new asset. NOTE: the simulation mesh "
			"is populated through the cloth Dataflow graph (a 'Skeletal Mesh Import' node referencing the "
			"source); open the asset's Dataflow to finish authoring. Creates a real asset (was a stub in v4)."))
		.StringArg(TEXT("asset_path"), TEXT("Destination /Game path for the cloth asset."), true)
		.StringArg(TEXT("source_skeletal_mesh"), TEXT("Skeletal mesh asset to drive cloth simulation."), true)
		.SupportsDryRun()
		.LongRunning()
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			FString AssetPath, SkelPath;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("asset_path"), AssetPath));
			BAIL_IF_INVALID(FMCPValidate::PackagePath(AssetPath));
			BAIL_IF_INVALID(FMCPValidate::AssetDoesNotExist(AssetPath));
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("source_skeletal_mesh"), SkelPath));

			USkeletalMesh* Skel = LoadObject<USkeletalMesh>(nullptr, *SkelPath);
			if (!Skel)
			{
				FString Stripped = SkelPath; int32 D;
				if (Stripped.FindLastChar(TEXT('.'), D)) Stripped = Stripped.Left(D);
				Skel = LoadObject<USkeletalMesh>(nullptr, *Stripped);
			}
			if (!Skel) return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
				FString::Printf(TEXT("Skeletal mesh not found: %s. Nothing was created."), *SkelPath));

			UPackage* Package = CreatePackage(*AssetPath);
			const FString AssetName = FPackageName::GetLongPackageAssetName(AssetPath);

			// Engine-provided non-modal helper (designed for scripted/agent flows).
			// TemplatePath nullptr => default empty Dataflow; bEmbedDataflow true keeps it self-contained.
			UObject* ClothObj = UChaosClothAssetFactory::CreateClothAssetFromTemplate(
				UChaosClothAsset::StaticClass(), Package, FName(*AssetName),
				RF_Public | RF_Standalone | RF_Transactional, /*TemplatePath*/ nullptr, /*bEmbedDataflow*/ true);

			UChaosClothAsset* Cloth = Cast<UChaosClothAsset>(ClothObj);
			if (!Cloth) return FMCPToolResult::ErrorStructured(EMCPError::Internal,
				TEXT("Cloth asset factory returned no asset."));

			FAssetRegistryModule::AssetCreated(Cloth);
			Package->MarkPackageDirty();
			// Journal the creation so run_tool_script can report truthfully that this
			// asset survives a rollback (UE package creation is not transactional).
			MCPCommon::NoteAssetCreated(AssetPath);

			TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
			Out->SetStringField(TEXT("asset"), Cloth->GetPathName());
			Out->SetStringField(TEXT("source_skeletal_mesh"), Skel->GetPathName());
			Out->SetStringField(TEXT("next_step"),
				TEXT("Open the asset and add a 'Skeletal Mesh Import' node in its Dataflow graph referencing the source, then build."));
			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Created cloth asset '%s' (source '%s'). Finish the sim mesh in the cloth Dataflow graph."),
					*AssetPath, *Skel->GetName()),
				Out);
		});

	// ================================================================
	// chaos_apply_force — actually implementable: applies impulse to any
	// actor with a simulating primitive component. No Chaos plugin dep.
	// ================================================================
	MCP_TOOL(Registry, "chaos_apply_force")
		.Description(TEXT(
			"Apply a one-shot impulse to an actor's primary primitive component. The component must "
			"have physics simulation enabled. Works in PIE — applies in editor world only as a stress test."))
		.StringArg(TEXT("actor_label"), TEXT("Editor label of the target actor."), true)
		.NumberArg(TEXT("force_x"), TEXT("Impulse X (uu * mass)."), true)
		.NumberArg(TEXT("force_y"), TEXT("Impulse Y."), true)
		.NumberArg(TEXT("force_z"), TEXT("Impulse Z."), true)
		.BoolArg(TEXT("velocity_change"), TEXT("If true, treat as velocity change (ignore mass)."))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			FString Label;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("actor_label"), Label));

			UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
			if (GEditor && GEditor->PlayWorld) World = GEditor->PlayWorld;
			if (!World) return FMCPToolResult::ErrorStructured(EMCPError::Internal,
				TEXT("No world available"));

			AActor* Actor = FindActorByLabel(World, Label);
			if (!Actor) return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
				FString::Printf(TEXT("Actor '%s' not found"), *Label));

			UPrimitiveComponent* Prim = Actor->FindComponentByClass<UPrimitiveComponent>();
			if (!Prim) return FMCPToolResult::ErrorStructured(EMCPError::Unsupported,
				FString::Printf(TEXT("Actor '%s' has no primitive component"), *Label));

			if (!Prim->IsSimulatingPhysics())
			{
				return FMCPToolResult::ErrorStructured(EMCPError::Unsupported,
					FString::Printf(TEXT("Component on '%s' is not simulating physics"), *Label),
					TEXT("Enable 'Simulate Physics' on the primitive component first."));
			}

			const FVector F(
				Args->GetNumberField(TEXT("force_x")),
				Args->GetNumberField(TEXT("force_y")),
				Args->GetNumberField(TEXT("force_z")));
			bool bVelChange = false;
			Args->TryGetBoolField(TEXT("velocity_change"), bVelChange);
			Prim->AddImpulse(F, NAME_None, bVelChange);

			TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetStringField(TEXT("actor"), Label);
			R->SetNumberField(TEXT("force_x"), F.X);
			R->SetNumberField(TEXT("force_y"), F.Y);
			R->SetNumberField(TEXT("force_z"), F.Z);
			R->SetBoolField(TEXT("velocity_change"), bVelChange);
			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Impulse applied to '%s'"), *Label), R);
		});
}

} // namespace MCPChaosTools
