// Copyright StraySpark Studio 2026. All Rights Reserved.
//
// Phase D.6 (Modeling) — geometry editing tools. v4 Phase 3: implemented for
// real via GeometryScript (GeometryScriptingCore), which exposes the modeling
// operations as plain function calls on UDynamicMesh — no editor-mode / tool-
// activation plumbing needed (the blocker that kept these as stubs in v3).
//
// Pipeline per tool: StaticMesh -> UDynamicMesh (CopyMeshFromStaticMeshV2)
// -> GeometryScript op -> write back (CopyMeshToStaticMesh, LOD0).
// NOTE: edits modify the ASSET — every instance of the mesh updates.

#include "Tools/MCPModelingTools.h"

#include "MCPToolRegistry.h"
#include "MCPToolBuilder.h"
#include "MCPProtocol.h"
#include "MCPValidate.h"
#include "Common/MCPActorResolver.h"
#include "Common/MCPEditorContext.h"

#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Actor.h"
#include "UDynamicMesh.h"
#include "GeometryScript/MeshAssetFunctions.h"
#include "GeometryScript/MeshBooleanFunctions.h"
#include "GeometryScript/MeshRemeshFunctions.h"
#include "GeometryScript/MeshModelingFunctions.h"
#include "GeometryScript/MeshUVFunctions.h"
#include "GeometryScript/MeshSelectionFunctions.h"
#include "GeometryScript/MeshQueryFunctions.h"

namespace MCPModelingTools
{

namespace
{
	/** Actor label -> its StaticMeshComponent's mesh asset + world transform. */
	UStaticMesh* ResolveActorMesh(const FString& Label, FTransform& OutWorldTransform, FMCPToolResult& OutError)
	{
		UWorld* World = MCPCommon::GetEditorWorldChecked(OutError);
		if (!World) { return nullptr; }

		AActor* Actor = MCPCommon::FindActorByLabel(World, Label);
		if (!Actor)
		{
			OutError = FMCPToolResult::ErrorStructured(EMCPError::NotFound,
				FString::Printf(TEXT("Actor not found: %s"), *Label));
			return nullptr;
		}
		UStaticMeshComponent* SMC = Actor->FindComponentByClass<UStaticMeshComponent>();
		if (!SMC || !SMC->GetStaticMesh())
		{
			OutError = FMCPToolResult::ErrorStructured(EMCPError::NotFound,
				FString::Printf(TEXT("Actor '%s' has no StaticMeshComponent with a mesh assigned."), *Label));
			return nullptr;
		}
		OutWorldTransform = SMC->GetComponentTransform();
		return SMC->GetStaticMesh();
	}

	/** StaticMesh asset -> fresh transient UDynamicMesh (LOD0 source model). */
	UDynamicMesh* ReadMesh(UStaticMesh* StaticMesh, FMCPToolResult& OutError)
	{
		UDynamicMesh* DynMesh = NewObject<UDynamicMesh>(GetTransientPackage());
		FGeometryScriptCopyMeshFromAssetOptions AssetOptions;
		FGeometryScriptMeshReadLOD ReadLOD;   // defaults: LOD0
		EGeometryScriptOutcomePins Outcome = EGeometryScriptOutcomePins::Failure;
		UGeometryScriptLibrary_StaticMeshFunctions::CopyMeshFromStaticMeshV2(
			StaticMesh, DynMesh, AssetOptions, ReadLOD, Outcome);
		if (Outcome != EGeometryScriptOutcomePins::Success)
		{
			OutError = FMCPToolResult::ErrorStructured(EMCPError::Internal,
				FString::Printf(TEXT("Could not read mesh data from '%s' (no accessible source model?)."),
					*StaticMesh->GetPathName()));
			return nullptr;
		}
		return DynMesh;
	}

	/** Write a UDynamicMesh back into the asset's LOD0 and mark it dirty. */
	bool WriteMesh(UDynamicMesh* DynMesh, UStaticMesh* StaticMesh, FMCPToolResult& OutError)
	{
		FGeometryScriptCopyMeshToAssetOptions WriteOptions; // keep materials, no transaction (registry wraps us)
		FGeometryScriptMeshWriteLOD WriteLOD;               // defaults: LOD0
		EGeometryScriptOutcomePins Outcome = EGeometryScriptOutcomePins::Failure;
		UGeometryScriptLibrary_StaticMeshFunctions::CopyMeshToStaticMesh(
			DynMesh, StaticMesh, WriteOptions, WriteLOD, Outcome);
		if (Outcome != EGeometryScriptOutcomePins::Success)
		{
			OutError = FMCPToolResult::ErrorStructured(EMCPError::Internal,
				FString::Printf(TEXT("Failed to write modified mesh back to '%s'."), *StaticMesh->GetPathName()));
			return false;
		}
		StaticMesh->MarkPackageDirty();
		return true;
	}

	int32 TriangleCountOf(UDynamicMesh* DynMesh)
	{
		return UGeometryScriptLibrary_MeshQueryFunctions::GetNumTriangleIDs(DynMesh);
	}
}

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// modeling_boolean — CSG between two actors' meshes (GeometryScript)
	// ================================================================
	MCP_TOOL(Registry, "modeling_boolean")
		.Description(TEXT(
			"Apply a CSG boolean (union / intersect / subtract) between two static-mesh actors' meshes, in their "
			"current world arrangement. The result replaces actor_a's MESH ASSET (every instance updates); actor_b "
			"is left untouched — delete or hide it afterwards if it was only a cutting tool."))
		.Destructive()
		.SupportsDryRun()
		.StringArg(TEXT("actor_a"), TEXT("Primary actor (its mesh asset receives the result)."), true)
		.StringArg(TEXT("actor_b"), TEXT("Secondary actor (operand)."), true)
		.EnumArg(TEXT("operation"), TEXT("Boolean operation."),
			{ TEXT("union"), TEXT("intersect"), TEXT("subtract") }, true)
		.Example(TEXT("{\"actor_a\": \"Wall\", \"actor_b\": \"DoorCutter\", \"operation\": \"subtract\"}"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString LabelA, LabelB, OpStr;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("actor_a"), LabelA));
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("actor_b"), LabelB));
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("operation"), OpStr));
			BAIL_IF_INVALID(FMCPValidate::OneOf(OpStr, { TEXT("union"), TEXT("intersect"), TEXT("subtract") }, TEXT("operation")));

			FMCPToolResult Err;
			FTransform XformA, XformB;
			UStaticMesh* MeshA = ResolveActorMesh(LabelA, XformA, Err);
			if (!MeshA) { return Err; }
			UStaticMesh* MeshB = ResolveActorMesh(LabelB, XformB, Err);
			if (!MeshB) { return Err; }

			UDynamicMesh* DynA = ReadMesh(MeshA, Err);
			if (!DynA) { return Err; }
			UDynamicMesh* DynB = ReadMesh(MeshB, Err);
			if (!DynB) { return Err; }

			const int32 TrisBefore = TriangleCountOf(DynA);

			// Compute in A's local space so the written asset stays correct under
			// A's component transform: A at identity, B at its transform relative to A.
			const FTransform ToolRelative = XformB.GetRelativeTransform(XformA);

			EGeometryScriptBooleanOperation Op =
				OpStr == TEXT("union") ? EGeometryScriptBooleanOperation::Union :
				OpStr == TEXT("intersect") ? EGeometryScriptBooleanOperation::Intersection :
				EGeometryScriptBooleanOperation::Subtract;

			FGeometryScriptMeshBooleanOptions Options; // defaults: fill holes, simplify
			UGeometryScriptLibrary_MeshBooleanFunctions::ApplyMeshBoolean(
				DynA, FTransform::Identity, DynB, ToolRelative, Op, Options);

			const int32 TrisAfter = TriangleCountOf(DynA);
			if (TrisAfter == 0)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::Internal,
					FString::Printf(TEXT("Boolean '%s' produced an empty mesh — asset NOT modified."), *OpStr),
					TEXT("For 'intersect', the meshes must overlap; for 'subtract', actor_b must intersect actor_a."));
			}

			if (!WriteMesh(DynA, MeshA, Err)) { return Err; }

			TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
			Out->SetStringField(TEXT("modified_asset"), MeshA->GetPathName());
			Out->SetNumberField(TEXT("triangles_before"), TrisBefore);
			Out->SetNumberField(TEXT("triangles_after"), TrisAfter);
			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Boolean %s: '%s' %s '%s' -> %d triangles (was %d). Asset '%s' modified — all instances update."),
					*OpStr, *LabelA, *OpStr, *LabelB, TrisAfter, TrisBefore, *MeshA->GetPathName()),
				Out);
		});

	// ================================================================
	// modeling_polycut — plane cut (GeometryScript)
	// ================================================================
	MCP_TOOL(Registry, "modeling_polycut")
		.Description(TEXT(
			"Cut a static-mesh actor's mesh with a world-space plane (origin + normal). The side the normal points "
			"toward is removed; holes are filled. Modifies the MESH ASSET (every instance updates)."))
		.Destructive()
		.SupportsDryRun()
		.StringArg(TEXT("actor_label"), TEXT("Actor whose mesh to cut."), true)
		.NumberArg(TEXT("origin_x"), TEXT("Plane origin X (world)."), true)
		.NumberArg(TEXT("origin_y"), TEXT("Plane origin Y (world)."), true)
		.NumberArg(TEXT("origin_z"), TEXT("Plane origin Z (world)."), true)
		.NumberArg(TEXT("normal_x"), TEXT("Plane normal X."), true)
		.NumberArg(TEXT("normal_y"), TEXT("Plane normal Y."), true)
		.NumberArg(TEXT("normal_z"), TEXT("Plane normal Z."), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString Label;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("actor_label"), Label));
			double OX, OY, OZ, NX, NY, NZ;
			BAIL_IF_INVALID(FMCPValidate::RequiredNumber(Args, TEXT("origin_x"), OX));
			BAIL_IF_INVALID(FMCPValidate::RequiredNumber(Args, TEXT("origin_y"), OY));
			BAIL_IF_INVALID(FMCPValidate::RequiredNumber(Args, TEXT("origin_z"), OZ));
			BAIL_IF_INVALID(FMCPValidate::RequiredNumber(Args, TEXT("normal_x"), NX));
			BAIL_IF_INVALID(FMCPValidate::RequiredNumber(Args, TEXT("normal_y"), NY));
			BAIL_IF_INVALID(FMCPValidate::RequiredNumber(Args, TEXT("normal_z"), NZ));

			FVector WorldNormal(NX, NY, NZ);
			if (!WorldNormal.Normalize())
			{
				return FMCPToolResult::ErrorStructured(EMCPError::OutOfRange,
					TEXT("Plane normal must be non-zero."));
			}

			FMCPToolResult Err;
			FTransform ActorXform;
			UStaticMesh* Mesh = ResolveActorMesh(Label, ActorXform, Err);
			if (!Mesh) { return Err; }
			UDynamicMesh* Dyn = ReadMesh(Mesh, Err);
			if (!Dyn) { return Err; }

			const int32 TrisBefore = TriangleCountOf(Dyn);

			// Transform the world-space plane into the mesh's local space.
			const FVector LocalOrigin = ActorXform.InverseTransformPosition(FVector(OX, OY, OZ));
			const FVector LocalNormal = ActorXform.InverseTransformVectorNoScale(WorldNormal).GetSafeNormal();

			// ApplyMeshPlaneCut keeps the -Z side of the cut frame; build the frame
			// with +Z along the normal so "the side the normal points toward" goes.
			FTransform CutFrame(FRotationMatrix::MakeFromZ(LocalNormal).ToQuat(), LocalOrigin);
			FGeometryScriptMeshPlaneCutOptions Options; // bFillHoles = true
			UGeometryScriptLibrary_MeshBooleanFunctions::ApplyMeshPlaneCut(Dyn, CutFrame, Options);

			const int32 TrisAfter = TriangleCountOf(Dyn);
			if (TrisAfter == 0)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::OutOfRange,
					TEXT("The cut removed the entire mesh — asset NOT modified."),
					TEXT("Flip the normal to keep the other side, or move the plane origin."));
			}

			if (!WriteMesh(Dyn, Mesh, Err)) { return Err; }

			TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
			Out->SetStringField(TEXT("modified_asset"), Mesh->GetPathName());
			Out->SetNumberField(TEXT("triangles_before"), TrisBefore);
			Out->SetNumberField(TEXT("triangles_after"), TrisAfter);
			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Plane cut '%s': %d -> %d triangles. Asset '%s' modified."),
					*Label, TrisBefore, TrisAfter, *Mesh->GetPathName()),
				Out);
		});

	// ================================================================
	// modeling_polyextrude — extrude faces along normals (GeometryScript)
	// ================================================================
	MCP_TOOL(Registry, "modeling_polyextrude")
		.Description(TEXT(
			"Extrude faces of a static-mesh actor's mesh by a distance along the average face normal. "
			"Provide face_indices (triangle indices) to extrude a subset, or omit to extrude ALL faces. "
			"Modifies the MESH ASSET."))
		.Destructive()
		.SupportsDryRun()
		.StringArg(TEXT("actor_label"), TEXT("Actor whose mesh to extrude."), true)
		.NumberArg(TEXT("distance"), TEXT("Extrusion distance in world units (negative = inset)."), true)
		.StringArrayArg(TEXT("face_indices"), TEXT("Triangle indices to extrude (numeric). Omit to extrude all faces."))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString Label;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("actor_label"), Label));
			double Distance;
			BAIL_IF_INVALID(FMCPValidate::RequiredNumber(Args, TEXT("distance"), Distance));

			// v4.5: per-face selection. Accept a JSON array of triangle indices
			// (numbers or numeric strings). Empty/absent => extrude all faces.
			TArray<int32> FaceIndices;
			if (Args->HasField(TEXT("face_indices")))
			{
				const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
				if (Args->TryGetArrayField(TEXT("face_indices"), Arr) && Arr)
				{
					for (const TSharedPtr<FJsonValue>& V : *Arr)
					{
						if (!V.IsValid()) continue;
						double Num = 0.0;
						FString Str;
						if (V->TryGetNumber(Num))
						{
							FaceIndices.Add((int32)Num);
						}
						else if (V->TryGetString(Str) && Str.IsNumeric())
						{
							FaceIndices.Add(FCString::Atoi(*Str));
						}
						else
						{
							return FMCPToolResult::ErrorStructured(EMCPError::OutOfRange,
								FString::Printf(TEXT("face_indices contains a non-numeric entry: '%s'"), *Str));
						}
					}
				}
			}

			FMCPToolResult Err;
			FTransform ActorXform;
			UStaticMesh* Mesh = ResolveActorMesh(Label, ActorXform, Err);
			if (!Mesh) { return Err; }
			UDynamicMesh* Dyn = ReadMesh(Mesh, Err);
			if (!Dyn) { return Err; }

			const int32 TrisBefore = TriangleCountOf(Dyn);

			FGeometryScriptMeshSelection Selection;
			if (FaceIndices.Num() > 0)
			{
				// Validate indices are within range before building the selection.
				for (int32 Idx : FaceIndices)
				{
					if (Idx < 0 || Idx >= TrisBefore)
					{
						return FMCPToolResult::ErrorStructured(EMCPError::OutOfRange,
							FString::Printf(TEXT("Triangle index %d out of range (mesh has %d triangles)."), Idx, TrisBefore));
					}
				}
				UGeometryScriptLibrary_MeshSelectionFunctions::ConvertIndexArrayToMeshSelection(
					Dyn, FaceIndices, EGeometryScriptMeshSelectionType::Triangles, Selection);
			}
			else
			{
				UGeometryScriptLibrary_MeshSelectionFunctions::CreateSelectAllMeshSelection(
					Dyn, Selection, EGeometryScriptMeshSelectionType::Triangles);
			}

			FGeometryScriptMeshLinearExtrudeOptions Options;
			Options.Distance = (float)Distance;
			Options.DirectionMode = EGeometryScriptLinearExtrudeDirection::AverageFaceNormal;
			UGeometryScriptLibrary_MeshModelingFunctions::ApplyMeshLinearExtrudeFaces(Dyn, Options, Selection);

			const int32 TrisAfter = TriangleCountOf(Dyn);
			if (!WriteMesh(Dyn, Mesh, Err)) { return Err; }

			const bool bSubset = FaceIndices.Num() > 0;
			TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
			Out->SetStringField(TEXT("modified_asset"), Mesh->GetPathName());
			Out->SetNumberField(TEXT("faces_extruded"), bSubset ? FaceIndices.Num() : TrisBefore);
			Out->SetNumberField(TEXT("triangles_before"), TrisBefore);
			Out->SetNumberField(TEXT("triangles_after"), TrisAfter);
			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Extruded %s of '%s' by %.1f: %d -> %d triangles. Asset '%s' modified."),
					bSubset ? *FString::Printf(TEXT("%d face(s)"), FaceIndices.Num()) : TEXT("all faces"),
					*Label, Distance, TrisBefore, TrisAfter, *Mesh->GetPathName()),
				Out);
		});

	// ================================================================
	// modeling_uv_unwrap — UV projection / auto-unwrap (GeometryScript)
	// ================================================================
	MCP_TOOL(Registry, "modeling_uv_unwrap")
		.Description(TEXT(
			"Generate UVs for a static mesh asset's UV channel. Methods: planar / box / cylindrical projections "
			"(sized to the mesh bounds), or 'auto' (XAtlas auto-unwrap, best general choice)."))
		.SupportsDryRun()
		.StringArg(TEXT("asset_path"), TEXT("Static mesh asset path."), true)
		.IntArg(TEXT("uv_channel"), TEXT("UV channel index (default 0)."))
		.EnumArg(TEXT("method"), TEXT("Unwrap method (default: auto)."),
			{ TEXT("planar"), TEXT("cylindrical"), TEXT("box"), TEXT("auto") })
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("asset_path"), AssetPath));
			// NOTE: deliberately no AssetExists() here — it checks the on-DISK
			// package, which fails for freshly created/duplicated assets that are
			// only in memory (breaks create->edit run_tool_script chains). The
			// LoadObject below resolves in-memory assets fine.

			FString Method = TEXT("auto");
			Args->TryGetStringField(TEXT("method"), Method);
			int32 UVChannel = 0;
			if (Args->HasField(TEXT("uv_channel")))
			{
				UVChannel = FMath::Clamp((int32)Args->GetNumberField(TEXT("uv_channel")), 0, 7);
			}

			UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *AssetPath);
			if (!Mesh)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
					FString::Printf(TEXT("Static mesh not found: %s"), *AssetPath));
			}

			FMCPToolResult Err;
			UDynamicMesh* Dyn = ReadMesh(Mesh, Err);
			if (!Dyn) { return Err; }

			// Make sure the UV channel exists.
			UGeometryScriptLibrary_MeshUVFunctions::SetNumUVSets(Dyn, UVChannel + 1);

			// Projections are sized to the mesh's local bounds so 1 UV tile spans the mesh.
			const FBox Bounds = Mesh->GetBoundingBox();
			const FVector Extent = Bounds.GetExtent().ComponentMax(FVector(1.0));
			const FTransform ProjectionFrame(FQuat::Identity, Bounds.GetCenter(), Extent);
			FGeometryScriptMeshSelection WholeMesh; // empty selection = whole mesh

			if (Method == TEXT("planar"))
			{
				UGeometryScriptLibrary_MeshUVFunctions::SetMeshUVsFromPlanarProjection(
					Dyn, UVChannel, ProjectionFrame, WholeMesh);
			}
			else if (Method == TEXT("box"))
			{
				UGeometryScriptLibrary_MeshUVFunctions::SetMeshUVsFromBoxProjection(
					Dyn, UVChannel, ProjectionFrame, WholeMesh);
			}
			else if (Method == TEXT("cylindrical"))
			{
				UGeometryScriptLibrary_MeshUVFunctions::SetMeshUVsFromCylinderProjection(
					Dyn, UVChannel, ProjectionFrame, WholeMesh);
			}
			else if (Method == TEXT("auto"))
			{
				FGeometryScriptXAtlasOptions Options;
				UGeometryScriptLibrary_MeshUVFunctions::AutoGenerateXAtlasMeshUVs(Dyn, UVChannel, Options);
			}
			else
			{
				return FMCPToolResult::ErrorStructured(EMCPError::OutOfRange,
					FString::Printf(TEXT("Unknown method '%s'."), *Method),
					TEXT("Valid: planar, cylindrical, box, auto."));
			}

			if (!WriteMesh(Dyn, Mesh, Err)) { return Err; }

			TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
			Out->SetStringField(TEXT("asset"), Mesh->GetPathName());
			Out->SetNumberField(TEXT("uv_channel"), UVChannel);
			Out->SetStringField(TEXT("method"), Method);
			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Generated %s UVs on channel %d of '%s'."), *Method, UVChannel, *AssetPath),
				Out);
		});

	// ================================================================
	// modeling_remesh — uniform remesh to target triangle count
	// ================================================================
	MCP_TOOL(Registry, "modeling_remesh")
		.Description(TEXT(
			"Uniformly remesh a static mesh asset toward a target triangle count (use for topology cleanup or "
			"densification; for pure reduction prefer the simplify pipeline). Can be slow on dense meshes."))
		.LongRunning()
		.SupportsDryRun()
		.StringArg(TEXT("asset_path"), TEXT("Static mesh asset path."), true)
		.IntArg(TEXT("target_triangles"), TEXT("Approximate target triangle count (100 - 2,000,000)."), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("asset_path"), AssetPath));
			// NOTE: deliberately no AssetExists() here — it checks the on-DISK
			// package, which fails for freshly created/duplicated assets that are
			// only in memory (breaks create->edit run_tool_script chains). The
			// LoadObject below resolves in-memory assets fine.
			double TargetTrisD;
			BAIL_IF_INVALID(FMCPValidate::RequiredNumber(Args, TEXT("target_triangles"), TargetTrisD));
			const int32 TargetTris = (int32)FMath::Clamp(TargetTrisD, 100.0, 2000000.0);

			UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *AssetPath);
			if (!Mesh)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
					FString::Printf(TEXT("Static mesh not found: %s"), *AssetPath));
			}

			FMCPToolResult Err;
			UDynamicMesh* Dyn = ReadMesh(Mesh, Err);
			if (!Dyn) { return Err; }

			const int32 TrisBefore = TriangleCountOf(Dyn);

			FGeometryScriptRemeshOptions RemeshOptions;          // defaults
			FGeometryScriptUniformRemeshOptions UniformOptions;
			UniformOptions.TargetType = EGeometryScriptUniformRemeshTargetType::TriangleCount;
			UniformOptions.TargetTriangleCount = TargetTris;
			UGeometryScriptLibrary_RemeshingFunctions::ApplyUniformRemesh(Dyn, RemeshOptions, UniformOptions);

			const int32 TrisAfter = TriangleCountOf(Dyn);
			if (!WriteMesh(Dyn, Mesh, Err)) { return Err; }

			TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
			Out->SetStringField(TEXT("asset"), Mesh->GetPathName());
			Out->SetNumberField(TEXT("triangles_before"), TrisBefore);
			Out->SetNumberField(TEXT("triangles_after"), TrisAfter);
			Out->SetNumberField(TEXT("target"), TargetTris);
			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Remeshed '%s': %d -> %d triangles (target %d)."),
					*AssetPath, TrisBefore, TrisAfter, TargetTris),
				Out);
		});
}

} // namespace MCPModelingTools
