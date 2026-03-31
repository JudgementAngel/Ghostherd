#include "Tools/MCPPhysicsTools.h"
#include "MCPToolRegistry.h"
#include "MCPProtocol.h"

#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Components/PrimitiveComponent.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "PhysicsEngine/PhysicsConstraintActor.h"
#include "PhysicsEngine/BodyInstance.h"

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
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if ((*It)->GetActorLabel() == Label) return *It;
	}
	return nullptr;
}

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// set_physics_simulation - Enable/disable physics on an actor
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("actor_name"), TEXT("Label of the actor to configure physics on"), true);
		FMCPSchemaBuilder::AddBoolean(Schema, TEXT("simulate_physics"), TEXT("Enable or disable physics simulation on the root primitive component"));
		FMCPSchemaBuilder::AddBoolean(Schema, TEXT("enable_gravity"), TEXT("Enable or disable gravity on the component"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("mass_kg"), TEXT("Override the mass in kilograms. Use 0 or omit to clear the override."));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("linear_damping"), TEXT("Linear damping coefficient (drag). Higher values slow linear movement faster."));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("angular_damping"), TEXT("Angular damping coefficient (rotational drag). Higher values slow rotation faster."));
		FMCPSchemaBuilder::AddBoolean(Schema, TEXT("lock_x_translation"), TEXT("Lock movement along the world X axis"));
		FMCPSchemaBuilder::AddBoolean(Schema, TEXT("lock_y_translation"), TEXT("Lock movement along the world Y axis"));
		FMCPSchemaBuilder::AddBoolean(Schema, TEXT("lock_z_translation"), TEXT("Lock movement along the world Z axis"));
		FMCPSchemaBuilder::AddBoolean(Schema, TEXT("lock_x_rotation"), TEXT("Lock rotation around the world X axis"));
		FMCPSchemaBuilder::AddBoolean(Schema, TEXT("lock_y_rotation"), TEXT("Lock rotation around the world Y axis"));
		FMCPSchemaBuilder::AddBoolean(Schema, TEXT("lock_z_rotation"), TEXT("Lock rotation around the world Z axis"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("set_physics_simulation");
		Def.Description = TEXT("Enable or disable physics simulation on an actor's root PrimitiveComponent and configure physical properties: mass, damping, gravity, and per-axis translation/rotation locks.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
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
		Def.bIdempotentHint = true;
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// set_collision_profile - Set the collision preset/profile on an actor
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("actor_name"), TEXT("Label of the actor to configure collision on"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("profile_name"), TEXT("Named collision preset to apply (e.g. 'BlockAll', 'OverlapAll', 'NoCollision', 'Pawn', 'PhysicsActor', 'Trigger'). Takes priority over other settings when provided."));
		FMCPSchemaBuilder::AddEnum(Schema, TEXT("collision_enabled"), TEXT("Type of collision to enable"),
			{ TEXT("NoCollision"), TEXT("QueryOnly"), TEXT("PhysicsOnly"), TEXT("QueryAndPhysics") });
		FMCPSchemaBuilder::AddBoolean(Schema, TEXT("generate_overlap_events"), TEXT("Whether the component generates overlap events when it overlaps other components"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("set_collision_profile");
		Def.Description = TEXT("Set the collision profile (preset) and/or collision enabled type on an actor's root PrimitiveComponent. Use profile_name for named presets like 'BlockAll' or 'PhysicsActor', or use collision_enabled for explicit control.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
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
		Def.bIdempotentHint = true;
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// add_physics_constraint - Spawn a PhysicsConstraintActor linking two actors
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("actor_name_1"), TEXT("Label of the first actor to constrain"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("actor_name_2"), TEXT("Label of the second actor to constrain"), true);
		FMCPSchemaBuilder::AddEnum(Schema, TEXT("constraint_type"), TEXT("Type of physics constraint to create"),
			{ TEXT("Fixed"), TEXT("Hinge"), TEXT("Prismatic"), TEXT("BallSocket"), TEXT("Free") });
		FMCPSchemaBuilder::AddString(Schema, TEXT("label"), TEXT("Label for the spawned PhysicsConstraintActor in the scene outliner"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("x"), TEXT("World X position of the constraint (default: midpoint between actors)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("y"), TEXT("World Y position of the constraint (default: midpoint between actors)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("z"), TEXT("World Z position of the constraint (default: midpoint between actors)"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("add_physics_constraint");
		Def.Description = TEXT("Spawn an APhysicsConstraintActor that links two actors with a named constraint type. Fixed locks all motion, Hinge allows rotation on one axis, Prismatic allows sliding on one axis, BallSocket allows free rotation, Free allows all motion.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
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
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// get_physics_info - Read physics state from an actor
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("actor_name"), TEXT("Label of the actor to inspect"), true);

		FMCPToolDefinition Def;
		Def.Name = TEXT("get_physics_info");
		Def.Description = TEXT("Get a comprehensive physics report for an actor: simulation state, mass, damping, gravity, collision profile, bounds, velocity, center of mass, and inertia tensor.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
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
				return FMCPToolResult::Success(JsonToString(Result));
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

			return FMCPToolResult::Success(JsonToString(Result));
		});
		Def.bReadOnlyHint = true;
		Def.bIdempotentHint = true;
		Registry.RegisterTool(Def);
	}

} // RegisterAll

} // namespace MCPPhysicsTools
