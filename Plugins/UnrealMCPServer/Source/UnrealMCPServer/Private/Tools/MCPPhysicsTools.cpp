// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "Tools/MCPPhysicsTools.h"
#include "Common/MCPActorResolver.h"
#include "MCPToolRegistry.h"
#include "MCPToolBuilder.h"
#include "MCPProtocol.h"

#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Components/PrimitiveComponent.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "PhysicsEngine/PhysicsConstraintActor.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "AssetToolsModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "Factories/Factory.h"
#include "Common/MCPAssetCreate.h"

namespace MCPPhysicsTools
{

static UWorld* GetEditorWorld()
{
	if (GEditor)
	{
		return GEditor->GetEditorWorldContext().World();
	}
	return nullptr;
}

static AActor* FindActorByLabel(UWorld* World, const FString& Label)
{
	// v4 Phase 1: cached resolver (O(1) amortized) replaces the per-call actor scan.
	return MCPCommon::FindActorByLabel(World, Label);
}

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// set_physics_simulation - Enable/disable physics on an actor
	// ================================================================
	MCP_TOOL(Registry, "set_physics_simulation")
		.Description(TEXT("Enable or disable physics simulation on an actor's root PrimitiveComponent and configure physical properties: mass, damping, gravity, and per-axis translation/rotation locks."))
		.Idempotent()
		.StringArg(TEXT("actor_name"), TEXT("Label of the actor to configure physics on"), true)
		.BoolArg(TEXT("simulate_physics"), TEXT("Enable or disable physics simulation on the root primitive component"))
		.BoolArg(TEXT("enable_gravity"), TEXT("Enable or disable gravity on the component"))
		.NumberArg(TEXT("mass_kg"), TEXT("Override the mass in kilograms. Use 0 or omit to clear the override."))
		.NumberArg(TEXT("linear_damping"), TEXT("Linear damping coefficient (drag). Higher values slow linear movement faster."))
		.NumberArg(TEXT("angular_damping"), TEXT("Angular damping coefficient (rotational drag). Higher values slow rotation faster."))
		.BoolArg(TEXT("lock_x_translation"), TEXT("Lock movement along the world X axis"))
		.BoolArg(TEXT("lock_y_translation"), TEXT("Lock movement along the world Y axis"))
		.BoolArg(TEXT("lock_z_translation"), TEXT("Lock movement along the world Z axis"))
		.BoolArg(TEXT("lock_x_rotation"), TEXT("Lock rotation around the world X axis"))
		.BoolArg(TEXT("lock_y_rotation"), TEXT("Lock rotation around the world Y axis"))
		.BoolArg(TEXT("lock_z_rotation"), TEXT("Lock rotation around the world Z axis"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			FString ActorName;
			if (!Args->TryGetStringField(TEXT("actor_name"), ActorName))
				return FMCPToolResult::Error(TEXT("actor_name is required"));

			AActor* Actor = FindActorByLabel(World, ActorName);
			if (!Actor)
				return FMCPToolResult::Error(FString::Printf(TEXT("Actor not found: %s"), *ActorName));

			UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(Actor->GetRootComponent());
			if (!PrimComp)
				return FMCPToolResult::Error(FString::Printf(TEXT("Actor '%s' has no root PrimitiveComponent. Physics requires a PrimitiveComponent (e.g. StaticMeshComponent, SkeletalMeshComponent)."), *ActorName));

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Set Physics Simulation")));
			PrimComp->Modify();

			// Simulate physics
			bool bSimulate = false;
			if (Args->TryGetBoolField(TEXT("simulate_physics"), bSimulate))
			{
				PrimComp->SetSimulatePhysics(bSimulate);
			}

			// Gravity
			bool bGravity = false;
			if (Args->TryGetBoolField(TEXT("enable_gravity"), bGravity))
			{
				PrimComp->SetEnableGravity(bGravity);
			}

			// Mass override (kg)
			if (Args->HasField(TEXT("mass_kg")))
			{
				float MassKg = (float)Args->GetNumberField(TEXT("mass_kg"));
				if (MassKg > 0.0f)
				{
					// NAME_None means root body (no specific bone)
					PrimComp->SetMassOverrideInKg(NAME_None, MassKg, true);
				}
				else
				{
					// Clear the override
					PrimComp->SetMassOverrideInKg(NAME_None, 1.0f, false);
				}
			}

			// Linear damping
			if (Args->HasField(TEXT("linear_damping")))
			{
				PrimComp->SetLinearDamping((float)Args->GetNumberField(TEXT("linear_damping")));
			}

			// Angular damping
			if (Args->HasField(TEXT("angular_damping")))
			{
				PrimComp->SetAngularDamping((float)Args->GetNumberField(TEXT("angular_damping")));
			}

			// Per-axis locks via BodyInstance
			FBodyInstance& BI = PrimComp->BodyInstance;

			bool bLockXTrans = false;
			if (Args->TryGetBoolField(TEXT("lock_x_translation"), bLockXTrans))
			{
				BI.bLockXTranslation = bLockXTrans;
			}

			bool bLockYTrans = false;
			if (Args->TryGetBoolField(TEXT("lock_y_translation"), bLockYTrans))
			{
				BI.bLockYTranslation = bLockYTrans;
			}

			bool bLockZTrans = false;
			if (Args->TryGetBoolField(TEXT("lock_z_translation"), bLockZTrans))
			{
				BI.bLockZTranslation = bLockZTrans;
			}

			bool bLockXRot = false;
			if (Args->TryGetBoolField(TEXT("lock_x_rotation"), bLockXRot))
			{
				BI.bLockXRotation = bLockXRot;
			}

			bool bLockYRot = false;
			if (Args->TryGetBoolField(TEXT("lock_y_rotation"), bLockYRot))
			{
				BI.bLockYRotation = bLockYRot;
			}

			bool bLockZRot = false;
			if (Args->TryGetBoolField(TEXT("lock_z_rotation"), bLockZRot))
			{
				BI.bLockZRotation = bLockZRot;
			}

			// Push lock state to the physics handle if the body is already created
			BI.CreateDOFLock();

			PrimComp->PostEditChange();
			GEditor->EndTransaction();

			// Build a summary of what was applied
			TArray<FString> Applied;
			Applied.Add(FString::Printf(TEXT("simulates_physics=%s"), PrimComp->IsSimulatingPhysics() ? TEXT("true") : TEXT("false")));
			Applied.Add(FString::Printf(TEXT("gravity=%s"), PrimComp->IsGravityEnabled() ? TEXT("true") : TEXT("false")));
			Applied.Add(FString::Printf(TEXT("linear_damping=%.3f"), PrimComp->GetLinearDamping()));
			Applied.Add(FString::Printf(TEXT("angular_damping=%.3f"), PrimComp->GetAngularDamping()));
			Applied.Add(FString::Printf(TEXT("lock_translation=(%s,%s,%s)"),
				BI.bLockXTranslation ? TEXT("X") : TEXT("-"),
				BI.bLockYTranslation ? TEXT("Y") : TEXT("-"),
				BI.bLockZTranslation ? TEXT("Z") : TEXT("-")));
			Applied.Add(FString::Printf(TEXT("lock_rotation=(%s,%s,%s)"),
				BI.bLockXRotation ? TEXT("X") : TEXT("-"),
				BI.bLockYRotation ? TEXT("Y") : TEXT("-"),
				BI.bLockZRotation ? TEXT("Z") : TEXT("-")));

			return FMCPToolResult::Success(FString::Printf(TEXT("Physics configured on '%s': %s"),
				*ActorName, *FString::Join(Applied, TEXT(", "))));
		});

	// ================================================================
	// set_collision_profile - Set the collision preset/profile on an actor
	// ================================================================
	MCP_TOOL(Registry, "set_collision_profile")
		.Description(TEXT("Set the collision profile (preset) and/or collision enabled type on an actor's root PrimitiveComponent. Use profile_name for named presets like 'BlockAll' or 'PhysicsActor', or use collision_enabled for explicit control."))
		.Idempotent()
		.StringArg(TEXT("actor_name"), TEXT("Label of the actor to configure collision on"), true)
		.StringArg(TEXT("profile_name"), TEXT("Named collision preset to apply (e.g. 'BlockAll', 'OverlapAll', 'NoCollision', 'Pawn', 'PhysicsActor', 'Trigger'). Takes priority over other settings when provided."))
		.EnumArg(TEXT("collision_enabled"), TEXT("Type of collision to enable"),
			{ TEXT("NoCollision"), TEXT("QueryOnly"), TEXT("PhysicsOnly"), TEXT("QueryAndPhysics") })
		.BoolArg(TEXT("generate_overlap_events"), TEXT("Whether the component generates overlap events when it overlaps other components"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			FString ActorName;
			if (!Args->TryGetStringField(TEXT("actor_name"), ActorName))
				return FMCPToolResult::Error(TEXT("actor_name is required"));

			AActor* Actor = FindActorByLabel(World, ActorName);
			if (!Actor)
				return FMCPToolResult::Error(FString::Printf(TEXT("Actor not found: %s"), *ActorName));

			UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(Actor->GetRootComponent());
			if (!PrimComp)
				return FMCPToolResult::Error(FString::Printf(TEXT("Actor '%s' has no root PrimitiveComponent"), *ActorName));

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Set Collision Profile")));
			PrimComp->Modify();

			TArray<FString> Applied;

			// Named profile (takes priority — it will override object type and response channels)
			FString ProfileName;
			if (Args->TryGetStringField(TEXT("profile_name"), ProfileName) && !ProfileName.IsEmpty())
			{
				PrimComp->SetCollisionProfileName(FName(*ProfileName));
				Applied.Add(FString::Printf(TEXT("profile='%s'"), *ProfileName));
			}
			else
			{
				// Manual collision enabled type
				FString CollisionEnabledStr;
				if (Args->TryGetStringField(TEXT("collision_enabled"), CollisionEnabledStr))
				{
					ECollisionEnabled::Type CollisionType = ECollisionEnabled::QueryAndPhysics;
					if (CollisionEnabledStr == TEXT("NoCollision"))
					{
						CollisionType = ECollisionEnabled::NoCollision;
					}
					else if (CollisionEnabledStr == TEXT("QueryOnly"))
					{
						CollisionType = ECollisionEnabled::QueryOnly;
					}
					else if (CollisionEnabledStr == TEXT("PhysicsOnly"))
					{
						CollisionType = ECollisionEnabled::PhysicsOnly;
					}
					else if (CollisionEnabledStr == TEXT("QueryAndPhysics"))
					{
						CollisionType = ECollisionEnabled::QueryAndPhysics;
					}
					else
					{
						GEditor->EndTransaction();
						return FMCPToolResult::Error(FString::Printf(TEXT("Invalid collision_enabled value: '%s'. Valid values: NoCollision, QueryOnly, PhysicsOnly, QueryAndPhysics"), *CollisionEnabledStr));
					}
					PrimComp->SetCollisionEnabled(CollisionType);
					Applied.Add(FString::Printf(TEXT("collision_enabled='%s'"), *CollisionEnabledStr));
				}
			}

			// Overlap events
			bool bGenerateOverlap = false;
			if (Args->TryGetBoolField(TEXT("generate_overlap_events"), bGenerateOverlap))
			{
				PrimComp->SetGenerateOverlapEvents(bGenerateOverlap);
				Applied.Add(FString::Printf(TEXT("generate_overlap_events=%s"), bGenerateOverlap ? TEXT("true") : TEXT("false")));
			}

			if (Applied.Num() == 0)
			{
				GEditor->EndTransaction();
				return FMCPToolResult::Error(TEXT("No collision settings provided. Specify at least one of: profile_name, collision_enabled, generate_overlap_events"));
			}

			PrimComp->PostEditChange();
			GEditor->EndTransaction();

			return FMCPToolResult::Success(FString::Printf(TEXT("Collision updated on '%s': %s"),
				*ActorName, *FString::Join(Applied, TEXT(", "))));
		});

	// ================================================================
	// add_physics_constraint - Spawn a PhysicsConstraintActor linking two actors
	// ================================================================
	MCP_TOOL(Registry, "add_physics_constraint")
		.Description(TEXT("Spawn an APhysicsConstraintActor that links two actors with a named constraint type. Fixed locks all motion, Hinge allows rotation on one axis, Prismatic allows sliding on one axis, BallSocket allows free rotation, Free allows all motion."))
		.StringArg(TEXT("actor_name_1"), TEXT("Label of the first actor to constrain"), true)
		.StringArg(TEXT("actor_name_2"), TEXT("Label of the second actor to constrain"), true)
		.EnumArg(TEXT("constraint_type"), TEXT("Type of physics constraint to create"),
			{ TEXT("Fixed"), TEXT("Hinge"), TEXT("Prismatic"), TEXT("BallSocket"), TEXT("Free") })
		.StringArg(TEXT("label"), TEXT("Label for the spawned PhysicsConstraintActor in the scene outliner"))
		.NumberArg(TEXT("x"), TEXT("World X position of the constraint (default: midpoint between actors)"))
		.NumberArg(TEXT("y"), TEXT("World Y position of the constraint (default: midpoint between actors)"))
		.NumberArg(TEXT("z"), TEXT("World Z position of the constraint (default: midpoint between actors)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			FString ActorName1, ActorName2;
			if (!Args->TryGetStringField(TEXT("actor_name_1"), ActorName1))
				return FMCPToolResult::Error(TEXT("actor_name_1 is required"));
			if (!Args->TryGetStringField(TEXT("actor_name_2"), ActorName2))
				return FMCPToolResult::Error(TEXT("actor_name_2 is required"));

			AActor* Actor1 = FindActorByLabel(World, ActorName1);
			if (!Actor1)
				return FMCPToolResult::Error(FString::Printf(TEXT("Actor not found: %s"), *ActorName1));

			AActor* Actor2 = FindActorByLabel(World, ActorName2);
			if (!Actor2)
				return FMCPToolResult::Error(FString::Printf(TEXT("Actor not found: %s"), *ActorName2));

			UPrimitiveComponent* Prim1 = Cast<UPrimitiveComponent>(Actor1->GetRootComponent());
			UPrimitiveComponent* Prim2 = Cast<UPrimitiveComponent>(Actor2->GetRootComponent());

			if (!Prim1)
				return FMCPToolResult::Error(FString::Printf(TEXT("Actor '%s' has no root PrimitiveComponent — cannot be constrained"), *ActorName1));
			if (!Prim2)
				return FMCPToolResult::Error(FString::Printf(TEXT("Actor '%s' has no root PrimitiveComponent — cannot be constrained"), *ActorName2));

			// Constraint type (default: Fixed)
			FString ConstraintTypeStr = TEXT("Fixed");
			Args->TryGetStringField(TEXT("constraint_type"), ConstraintTypeStr);

			// Spawn position: user-provided or midpoint between the two actors
			FVector Loc1 = Actor1->GetActorLocation();
			FVector Loc2 = Actor2->GetActorLocation();
			FVector SpawnPos(
				Args->HasField(TEXT("x")) ? Args->GetNumberField(TEXT("x")) : (Loc1.X + Loc2.X) * 0.5,
				Args->HasField(TEXT("y")) ? Args->GetNumberField(TEXT("y")) : (Loc1.Y + Loc2.Y) * 0.5,
				Args->HasField(TEXT("z")) ? Args->GetNumberField(TEXT("z")) : (Loc1.Z + Loc2.Z) * 0.5
			);
			FRotator SpawnRot = FRotator::ZeroRotator;

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Add Physics Constraint")));

			FActorSpawnParameters SpawnParams;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

			APhysicsConstraintActor* ConstraintActor = World->SpawnActor<APhysicsConstraintActor>(
				APhysicsConstraintActor::StaticClass(), SpawnPos, SpawnRot, SpawnParams);

			if (!ConstraintActor)
			{
				GEditor->EndTransaction();
				return FMCPToolResult::Error(TEXT("Failed to spawn PhysicsConstraintActor"));
			}

			// Apply label if provided
			FString Label;
			if (Args->TryGetStringField(TEXT("label"), Label) && !Label.IsEmpty())
			{
				ConstraintActor->SetActorLabel(Label);
			}
			else
			{
				ConstraintActor->SetActorLabel(FString::Printf(TEXT("Constraint_%s_%s"), *ActorName1, *ActorName2));
			}

			UPhysicsConstraintComponent* ConstraintComp = ConstraintActor->GetConstraintComp();
			if (!ConstraintComp)
			{
				ConstraintActor->Destroy();
				GEditor->EndTransaction();
				return FMCPToolResult::Error(TEXT("PhysicsConstraintActor has no ConstraintComponent"));
			}

			ConstraintComp->Modify();

			// Link the two actors' root primitives
			ConstraintComp->SetConstrainedComponents(Prim1, NAME_None, Prim2, NAME_None);

			// Configure constraint profile based on type
			FConstraintInstance& CI = ConstraintComp->ConstraintInstance;

			if (ConstraintTypeStr == TEXT("Fixed"))
			{
				// Lock all linear and angular motion
				CI.SetLinearXMotion(ELinearConstraintMotion::LCM_Locked);
				CI.SetLinearYMotion(ELinearConstraintMotion::LCM_Locked);
				CI.SetLinearZMotion(ELinearConstraintMotion::LCM_Locked);
				CI.SetAngularSwing1Motion(EAngularConstraintMotion::ACM_Locked);
				CI.SetAngularSwing2Motion(EAngularConstraintMotion::ACM_Locked);
				CI.SetAngularTwistMotion(EAngularConstraintMotion::ACM_Locked);
			}
			else if (ConstraintTypeStr == TEXT("Hinge"))
			{
				// Linear locked, twist free (rotation on one axis), swings locked
				CI.SetLinearXMotion(ELinearConstraintMotion::LCM_Locked);
				CI.SetLinearYMotion(ELinearConstraintMotion::LCM_Locked);
				CI.SetLinearZMotion(ELinearConstraintMotion::LCM_Locked);
				CI.SetAngularSwing1Motion(EAngularConstraintMotion::ACM_Locked);
				CI.SetAngularSwing2Motion(EAngularConstraintMotion::ACM_Locked);
				CI.SetAngularTwistMotion(EAngularConstraintMotion::ACM_Free);
			}
			else if (ConstraintTypeStr == TEXT("Prismatic"))
			{
				// One linear axis free, all angular locked
				CI.SetLinearXMotion(ELinearConstraintMotion::LCM_Free);
				CI.SetLinearYMotion(ELinearConstraintMotion::LCM_Locked);
				CI.SetLinearZMotion(ELinearConstraintMotion::LCM_Locked);
				CI.SetAngularSwing1Motion(EAngularConstraintMotion::ACM_Locked);
				CI.SetAngularSwing2Motion(EAngularConstraintMotion::ACM_Locked);
				CI.SetAngularTwistMotion(EAngularConstraintMotion::ACM_Locked);
			}
			else if (ConstraintTypeStr == TEXT("BallSocket"))
			{
				// Linear locked, all angular free
				CI.SetLinearXMotion(ELinearConstraintMotion::LCM_Locked);
				CI.SetLinearYMotion(ELinearConstraintMotion::LCM_Locked);
				CI.SetLinearZMotion(ELinearConstraintMotion::LCM_Locked);
				CI.SetAngularSwing1Motion(EAngularConstraintMotion::ACM_Free);
				CI.SetAngularSwing2Motion(EAngularConstraintMotion::ACM_Free);
				CI.SetAngularTwistMotion(EAngularConstraintMotion::ACM_Free);
			}
			else if (ConstraintTypeStr == TEXT("Free"))
			{
				// All linear and angular free
				CI.SetLinearXMotion(ELinearConstraintMotion::LCM_Free);
				CI.SetLinearYMotion(ELinearConstraintMotion::LCM_Free);
				CI.SetLinearZMotion(ELinearConstraintMotion::LCM_Free);
				CI.SetAngularSwing1Motion(EAngularConstraintMotion::ACM_Free);
				CI.SetAngularSwing2Motion(EAngularConstraintMotion::ACM_Free);
				CI.SetAngularTwistMotion(EAngularConstraintMotion::ACM_Free);
			}
			else
			{
				ConstraintActor->Destroy();
				GEditor->EndTransaction();
				return FMCPToolResult::Error(FString::Printf(TEXT("Invalid constraint_type: '%s'. Valid values: Fixed, Hinge, Prismatic, BallSocket, Free"), *ConstraintTypeStr));
			}

			ConstraintComp->PostEditChange();
			GEditor->EndTransaction();

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Created '%s' PhysicsConstraintActor '%s' between '%s' and '%s' at (%.1f, %.1f, %.1f)"),
				*ConstraintTypeStr,
				*ConstraintActor->GetActorLabel(),
				*ActorName1, *ActorName2,
				SpawnPos.X, SpawnPos.Y, SpawnPos.Z));
		});

	// ================================================================
	// get_physics_info - Read physics state from an actor
	// ================================================================
	MCP_TOOL(Registry, "get_physics_info")
		.Description(TEXT("Get a comprehensive physics report for an actor: simulation state, mass, damping, gravity, collision profile, bounds, velocity, center of mass, and inertia tensor."))
		.ReadOnly()
		.Idempotent()
		.StringArg(TEXT("actor_name"), TEXT("Label of the actor to inspect"), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			FString ActorName;
			if (!Args->TryGetStringField(TEXT("actor_name"), ActorName))
				return FMCPToolResult::Error(TEXT("actor_name is required"));

			AActor* Actor = FindActorByLabel(World, ActorName);
			if (!Actor)
				return FMCPToolResult::Error(FString::Printf(TEXT("Actor not found: %s"), *ActorName));

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("actor"), ActorName);
			Result->SetStringField(TEXT("class"), Actor->GetClass()->GetName());

			UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(Actor->GetRootComponent());
			if (!PrimComp)
			{
				Result->SetBoolField(TEXT("has_primitive_component"), false);
				Result->SetStringField(TEXT("note"), TEXT("Actor has no root PrimitiveComponent. Physics requires a PrimitiveComponent."));
				return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
			}

			Result->SetBoolField(TEXT("has_primitive_component"), true);
			Result->SetStringField(TEXT("component_class"), PrimComp->GetClass()->GetName());

			// Simulation state
			bool bSimulating = PrimComp->IsSimulatingPhysics();
			Result->SetBoolField(TEXT("simulates_physics"), bSimulating);
			Result->SetBoolField(TEXT("enable_gravity"), PrimComp->IsGravityEnabled());
			Result->SetBoolField(TEXT("is_physics_body"), PrimComp->BodyInstance.bSimulatePhysics);

			// Damping
			Result->SetNumberField(TEXT("linear_damping"), PrimComp->GetLinearDamping());
			Result->SetNumberField(TEXT("angular_damping"), PrimComp->GetAngularDamping());

			// Mass (queried from the body instance)
			const FBodyInstance& BI = PrimComp->BodyInstance;
			float Mass = PrimComp->GetMass();
			Result->SetNumberField(TEXT("mass_kg"), Mass);
			Result->SetBoolField(TEXT("mass_override_enabled"), BI.bOverrideMass);
			if (BI.bOverrideMass)
			{
				Result->SetNumberField(TEXT("mass_override_kg"), BI.GetMassOverride());
			}

			// Collision
			FName ProfileName = PrimComp->GetCollisionProfileName();
			Result->SetStringField(TEXT("collision_profile"), ProfileName.ToString());

			FString CollisionEnabledStr;
			switch (PrimComp->GetCollisionEnabled())
			{
			case ECollisionEnabled::NoCollision:       CollisionEnabledStr = TEXT("NoCollision"); break;
			case ECollisionEnabled::QueryOnly:         CollisionEnabledStr = TEXT("QueryOnly"); break;
			case ECollisionEnabled::PhysicsOnly:       CollisionEnabledStr = TEXT("PhysicsOnly"); break;
			case ECollisionEnabled::QueryAndPhysics:   CollisionEnabledStr = TEXT("QueryAndPhysics"); break;
			default:                                   CollisionEnabledStr = TEXT("Unknown"); break;
			}
			Result->SetStringField(TEXT("collision_enabled"), CollisionEnabledStr);
			Result->SetBoolField(TEXT("generate_overlap_events"), PrimComp->GetGenerateOverlapEvents());

			// World bounds
			FBoxSphereBounds Bounds = PrimComp->Bounds;
			TSharedPtr<FJsonObject> BoundsObj = MakeShared<FJsonObject>();
			BoundsObj->SetNumberField(TEXT("origin_x"), Bounds.Origin.X);
			BoundsObj->SetNumberField(TEXT("origin_y"), Bounds.Origin.Y);
			BoundsObj->SetNumberField(TEXT("origin_z"), Bounds.Origin.Z);
			BoundsObj->SetNumberField(TEXT("extent_x"), Bounds.BoxExtent.X);
			BoundsObj->SetNumberField(TEXT("extent_y"), Bounds.BoxExtent.Y);
			BoundsObj->SetNumberField(TEXT("extent_z"), Bounds.BoxExtent.Z);
			BoundsObj->SetNumberField(TEXT("sphere_radius"), Bounds.SphereRadius);
			Result->SetObjectField(TEXT("bounds"), BoundsObj);

			// Velocity (only meaningful while simulating)
			if (bSimulating)
			{
				FVector LinearVel = PrimComp->GetPhysicsLinearVelocity();
				FVector AngularVel = PrimComp->GetPhysicsAngularVelocityInDegrees();

				TSharedPtr<FJsonObject> VelObj = MakeShared<FJsonObject>();
				VelObj->SetNumberField(TEXT("linear_x"), LinearVel.X);
				VelObj->SetNumberField(TEXT("linear_y"), LinearVel.Y);
				VelObj->SetNumberField(TEXT("linear_z"), LinearVel.Z);
				VelObj->SetNumberField(TEXT("linear_speed"), LinearVel.Size());
				VelObj->SetNumberField(TEXT("angular_x_deg"), AngularVel.X);
				VelObj->SetNumberField(TEXT("angular_y_deg"), AngularVel.Y);
				VelObj->SetNumberField(TEXT("angular_z_deg"), AngularVel.Z);
				Result->SetObjectField(TEXT("velocity"), VelObj);
			}

			// BodyInstance details: center of mass and inertia tensor
			// These are only available if the physics body has been created
			if (BI.IsValidBodyInstance())
			{
				FVector CenterOfMass = BI.GetCOMPosition();
				TSharedPtr<FJsonObject> ComObj = MakeShared<FJsonObject>();
				ComObj->SetNumberField(TEXT("x"), CenterOfMass.X);
				ComObj->SetNumberField(TEXT("y"), CenterOfMass.Y);
				ComObj->SetNumberField(TEXT("z"), CenterOfMass.Z);
				Result->SetObjectField(TEXT("center_of_mass"), ComObj);

				FVector InertiaTensor = BI.GetBodyInertiaTensor();
				TSharedPtr<FJsonObject> InertiaObj = MakeShared<FJsonObject>();
				InertiaObj->SetNumberField(TEXT("x"), InertiaTensor.X);
				InertiaObj->SetNumberField(TEXT("y"), InertiaTensor.Y);
				InertiaObj->SetNumberField(TEXT("z"), InertiaTensor.Z);
				Result->SetObjectField(TEXT("inertia_tensor"), InertiaObj);
			}

			// Per-axis lock state
			TSharedPtr<FJsonObject> LocksObj = MakeShared<FJsonObject>();
			LocksObj->SetBoolField(TEXT("lock_x_translation"), BI.bLockXTranslation);
			LocksObj->SetBoolField(TEXT("lock_y_translation"), BI.bLockYTranslation);
			LocksObj->SetBoolField(TEXT("lock_z_translation"), BI.bLockZTranslation);
			LocksObj->SetBoolField(TEXT("lock_x_rotation"), BI.bLockXRotation);
			LocksObj->SetBoolField(TEXT("lock_y_rotation"), BI.bLockYRotation);
			LocksObj->SetBoolField(TEXT("lock_z_rotation"), BI.bLockZRotation);
			Result->SetObjectField(TEXT("axis_locks"), LocksObj);

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});

	// ================================================================
	// create_physics_material - Create a PhysicalMaterial asset
	// ================================================================
	MCP_TOOL(Registry, "create_physics_material")
		.Description(TEXT("Create a PhysicalMaterial asset with configurable friction, restitution (bounciness), and density. Use assign_physics_material to apply it to actors."))
		.StringArg(TEXT("asset_path"), TEXT("Content path for the new PhysicalMaterial (e.g., '/Game/Physics/PM_Ice')"), true)
		.NumberArg(TEXT("friction"), TEXT("Friction coefficient (default: 0.7)"))
		.NumberArg(TEXT("static_friction"), TEXT("Static friction override (default: same as friction)"))
		.NumberArg(TEXT("restitution"), TEXT("Bounciness 0-1 (default: 0.3)"))
		.NumberArg(TEXT("density"), TEXT("Density in kg/cm^3 (default: 1.0)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));

			FString PackagePath, AssetName;
			FMCPToolResult PackageError;
			UPackage* Package = MCPCommon::CreateAssetPackage(AssetPath, PackagePath, AssetName, PackageError);
			if (!Package) return PackageError;

			UPhysicalMaterial* NewMat = NewObject<UPhysicalMaterial>(Package, FName(*AssetName),
				RF_Public | RF_Standalone);
			if (!NewMat) return FMCPToolResult::Error(TEXT("Failed to create PhysicalMaterial"));

			if (Args->HasField(TEXT("friction")))
				NewMat->Friction = (float)Args->GetNumberField(TEXT("friction"));
			if (Args->HasField(TEXT("static_friction")))
				NewMat->StaticFriction = (float)Args->GetNumberField(TEXT("static_friction"));
			if (Args->HasField(TEXT("restitution")))
				NewMat->Restitution = (float)Args->GetNumberField(TEXT("restitution"));
			if (Args->HasField(TEXT("density")))
				NewMat->Density = (float)Args->GetNumberField(TEXT("density"));

			FAssetRegistryModule::AssetCreated(NewMat);
			Package->MarkPackageDirty();

			FString PackageFilename = FPackageName::LongPackageNameToFilename(PackagePath, FPackageName::GetAssetPackageExtension());
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
			UPackage::SavePackage(Package, NewMat, *PackageFilename, SaveArgs);

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Created PhysicalMaterial '%s' at %s (friction=%.2f, restitution=%.2f, density=%.2f)"),
				*AssetName, *AssetPath, NewMat->Friction, NewMat->Restitution, NewMat->Density));
		});

	// ================================================================
	// assign_physics_material - Assign PhysicalMaterial to actor
	// ================================================================
	MCP_TOOL(Registry, "assign_physics_material")
		.Description(TEXT("Assign a PhysicalMaterial to an actor's root PrimitiveComponent. Controls friction, bounciness, and density for physics interactions."))
		.StringArg(TEXT("actor_name"), TEXT("Label of the actor"), true)
		.StringArg(TEXT("material_path"), TEXT("Content path to the PhysicalMaterial asset"), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			FString ActorName, MatPath;
			if (!Args->TryGetStringField(TEXT("actor_name"), ActorName))
				return FMCPToolResult::Error(TEXT("actor_name is required"));
			if (!Args->TryGetStringField(TEXT("material_path"), MatPath))
				return FMCPToolResult::Error(TEXT("material_path is required"));

			AActor* Actor = FindActorByLabel(World, ActorName);
			if (!Actor) return FMCPToolResult::Error(FString::Printf(TEXT("Actor not found: %s"), *ActorName));

			UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(Actor->GetRootComponent());
			if (!PrimComp)
				return FMCPToolResult::Error(FString::Printf(TEXT("Actor '%s' has no root PrimitiveComponent"), *ActorName));

			UPhysicalMaterial* PhysMat = LoadObject<UPhysicalMaterial>(nullptr, *MatPath);
			if (!PhysMat)
				return FMCPToolResult::Error(FString::Printf(TEXT("PhysicalMaterial not found: %s"), *MatPath));

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Assign Physics Material")));
			PrimComp->Modify();

			PrimComp->BodyInstance.SetPhysMaterialOverride(PhysMat);

			GEditor->EndTransaction();

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Assigned PhysicalMaterial '%s' to actor '%s'"), *PhysMat->GetName(), *ActorName));
		});

	// ================================================================
	// get_physics_material_info - Read physics material properties
	// ================================================================
	MCP_TOOL(Registry, "get_physics_material_info")
		.Description(TEXT("Read properties of a PhysicalMaterial asset: friction, static friction, restitution (bounciness), density, and surface type. Use to inspect existing physics materials before assigning them."))
		.ReadOnly()
		.Idempotent()
		.StringArg(TEXT("asset_path"), TEXT("Content path to the PhysicalMaterial asset"), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));

			UPhysicalMaterial* PhysMat = LoadObject<UPhysicalMaterial>(nullptr, *AssetPath);
			if (!PhysMat)
				return FMCPToolResult::Error(FString::Printf(TEXT("PhysicalMaterial not found: %s"), *AssetPath));

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("name"), PhysMat->GetName());
			Result->SetStringField(TEXT("path"), PhysMat->GetPathName());
			Result->SetNumberField(TEXT("friction"), PhysMat->Friction);
			Result->SetNumberField(TEXT("static_friction"), PhysMat->StaticFriction);
			Result->SetNumberField(TEXT("restitution"), PhysMat->Restitution);
			Result->SetNumberField(TEXT("density"), PhysMat->Density);

			// Friction combine mode
			FString FrictionCombine;
			switch (PhysMat->FrictionCombineMode)
			{
				case EFrictionCombineMode::Average: FrictionCombine = TEXT("Average"); break;
				case EFrictionCombineMode::Min: FrictionCombine = TEXT("Min"); break;
				case EFrictionCombineMode::Multiply: FrictionCombine = TEXT("Multiply"); break;
				case EFrictionCombineMode::Max: FrictionCombine = TEXT("Max"); break;
				default: FrictionCombine = TEXT("Average"); break;
			}
			Result->SetStringField(TEXT("friction_combine_mode"), FrictionCombine);

			// Restitution combine mode
			FString RestitutionCombine;
			switch (PhysMat->RestitutionCombineMode)
			{
				case EFrictionCombineMode::Average: RestitutionCombine = TEXT("Average"); break;
				case EFrictionCombineMode::Min: RestitutionCombine = TEXT("Min"); break;
				case EFrictionCombineMode::Multiply: RestitutionCombine = TEXT("Multiply"); break;
				case EFrictionCombineMode::Max: RestitutionCombine = TEXT("Max"); break;
				default: RestitutionCombine = TEXT("Average"); break;
			}
			Result->SetStringField(TEXT("restitution_combine_mode"), RestitutionCombine);

			// Surface type
			Result->SetNumberField(TEXT("surface_type"), (int32)PhysMat->SurfaceType.GetValue());

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});

	// ================================================================
	// list_collision_channels - List all collision channels
	// ================================================================
	MCP_TOOL(Registry, "list_collision_channels")
		.Description(TEXT("List all collision channels (default engine channels + custom project channels) with their default responses. Use with set_collision_response to configure per-actor collision."))
		.ReadOnly()
		.Idempotent()
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			TArray<TSharedPtr<FJsonValue>> ChannelArray;

			// List all 32 possible channels
			for (int32 i = 0; i < ECC_MAX; i++)
			{
				ECollisionChannel Channel = (ECollisionChannel)i;
				FName ChannelName = UCollisionProfile::Get()->ReturnChannelNameFromContainerIndex(i);
				if (ChannelName == NAME_None) continue;

				TSharedPtr<FJsonObject> ChObj = MakeShared<FJsonObject>();
				ChObj->SetNumberField(TEXT("index"), i);
				ChObj->SetStringField(TEXT("name"), ChannelName.ToString());

				// Determine if custom or default
				bool bIsCustom = (i >= ECC_GameTraceChannel1 && i <= ECC_GameTraceChannel18);
				ChObj->SetBoolField(TEXT("is_custom"), bIsCustom);

				ChannelArray.Add(MakeShared<FJsonValueObject>(ChObj));
			}

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetNumberField(TEXT("count"), ChannelArray.Num());
			Result->SetArrayField(TEXT("channels"), ChannelArray);

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});

	// ================================================================
	// set_collision_response - Set per-channel collision response
	// ================================================================
	MCP_TOOL(Registry, "set_collision_response")
		.Description(TEXT("Set the collision response for a specific channel on an actor's root PrimitiveComponent. Use list_collision_channels to see available channels. Common channels: WorldStatic, WorldDynamic, Pawn, PhysicsBody, Vehicle, Destructible."))
		.StringArg(TEXT("actor_name"), TEXT("Label of the actor"), true)
		.StringArg(TEXT("channel"), TEXT("Collision channel name (e.g., 'WorldStatic', 'Pawn', 'PhysicsBody', 'Visibility')"), true)
		.EnumArg(TEXT("response"), TEXT("Collision response for this channel"),
			{ TEXT("Block"), TEXT("Overlap"), TEXT("Ignore") }, true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			FString ActorName, ChannelStr, ResponseStr;
			if (!Args->TryGetStringField(TEXT("actor_name"), ActorName))
				return FMCPToolResult::Error(TEXT("actor_name is required"));
			if (!Args->TryGetStringField(TEXT("channel"), ChannelStr))
				return FMCPToolResult::Error(TEXT("channel is required"));
			if (!Args->TryGetStringField(TEXT("response"), ResponseStr))
				return FMCPToolResult::Error(TEXT("response is required"));

			AActor* Actor = FindActorByLabel(World, ActorName);
			if (!Actor) return FMCPToolResult::Error(FString::Printf(TEXT("Actor not found: %s"), *ActorName));

			UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(Actor->GetRootComponent());
			if (!PrimComp)
				return FMCPToolResult::Error(FString::Printf(TEXT("Actor '%s' has no root PrimitiveComponent"), *ActorName));

			// Find channel by name
			ECollisionChannel FoundChannel = ECC_MAX;
			for (int32 i = 0; i < ECC_MAX; i++)
			{
				FName ChName = UCollisionProfile::Get()->ReturnChannelNameFromContainerIndex(i);
				if (ChName.ToString().Equals(ChannelStr, ESearchCase::IgnoreCase))
				{
					FoundChannel = (ECollisionChannel)i;
					break;
				}
			}

			if (FoundChannel == ECC_MAX)
				return FMCPToolResult::Error(FString::Printf(TEXT("Collision channel not found: '%s'. Use list_collision_channels to see valid channels."), *ChannelStr));

			ECollisionResponse Response;
			if (ResponseStr == TEXT("Block")) Response = ECR_Block;
			else if (ResponseStr == TEXT("Overlap")) Response = ECR_Overlap;
			else if (ResponseStr == TEXT("Ignore")) Response = ECR_Ignore;
			else return FMCPToolResult::Error(FString::Printf(TEXT("Invalid response: '%s'. Use Block, Overlap, or Ignore."), *ResponseStr));

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Set Collision Response")));
			PrimComp->Modify();

			PrimComp->SetCollisionResponseToChannel(FoundChannel, Response);

			GEditor->EndTransaction();

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Set collision response for '%s' channel '%s' = %s"),
				*ActorName, *ChannelStr, *ResponseStr));
		});
} // RegisterAll

} // namespace MCPPhysicsTools
