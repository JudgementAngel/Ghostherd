// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "Tools/MCPNetworkingTools.h"
#include "Common/MCPActorResolver.h"
#include "MCPToolRegistry.h"
#include "MCPProtocol.h"
#include "MCPToolBuilder.h"

#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"

namespace MCPNetworkingTools
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

static FString NetDormancyToString(ENetDormancy Dormancy)
{
	switch (Dormancy)
	{
	case DORM_Never:          return TEXT("DORM_Never");
	case DORM_Awake:          return TEXT("DORM_Awake");
	case DORM_DormantAll:     return TEXT("DORM_DormantAll");
	case DORM_DormantPartial: return TEXT("DORM_DormantPartial");
	case DORM_Initial:        return TEXT("DORM_Initial");
	default:                  return TEXT("DORM_Unknown");
	}
}

static FString NetRoleToString(ENetRole Role)
{
	switch (Role)
	{
	case ROLE_None:             return TEXT("ROLE_None");
	case ROLE_SimulatedProxy:   return TEXT("ROLE_SimulatedProxy");
	case ROLE_AutonomousProxy:  return TEXT("ROLE_AutonomousProxy");
	case ROLE_Authority:        return TEXT("ROLE_Authority");
	default:                    return TEXT("ROLE_Unknown");
	}
}

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// get_replication_info - Read replication settings from an actor
	// ================================================================
	MCP_TOOL(Registry, "get_replication_info")
		.Description(TEXT("Get a full report of the network replication settings on an actor: bReplicates, bReplicateMovement, bAlwaysRelevant, bOnlyRelevantToOwner, NetUpdateFrequency, MinNetUpdateFrequency, NetPriority, NetDormancy, and the current net role."))
		.ReadOnly()
		.Idempotent()
		.StringArg(TEXT("actor_name"), TEXT("Label of the actor to inspect for replication settings"), true)
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
			Result->SetStringField(TEXT("actor"),                   ActorName);
			Result->SetStringField(TEXT("class"),                   Actor->GetClass()->GetName());
			Result->SetBoolField  (TEXT("bReplicates"),             Actor->GetIsReplicated());
			Result->SetBoolField  (TEXT("bReplicateMovement"),      Actor->IsReplicatingMovement());
			Result->SetBoolField  (TEXT("bAlwaysRelevant"),         Actor->bAlwaysRelevant);
			Result->SetBoolField  (TEXT("bOnlyRelevantToOwner"),    Actor->bOnlyRelevantToOwner);
			Result->SetNumberField(TEXT("NetUpdateFrequency"),      Actor->GetNetUpdateFrequency());
			Result->SetNumberField(TEXT("MinNetUpdateFrequency"),   Actor->GetMinNetUpdateFrequency());
			Result->SetNumberField(TEXT("NetPriority"),             Actor->NetPriority);
			Result->SetStringField(TEXT("NetDormancy"),             NetDormancyToString(Actor->NetDormancy));
			Result->SetStringField(TEXT("net_role"),                NetRoleToString(Actor->GetLocalRole()));

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});

	// ================================================================
	// set_replication_settings - Configure actor replication properties
	// ================================================================
	MCP_TOOL(Registry, "set_replication_settings")
		.Description(TEXT("Configure network replication settings on an actor. Only the parameters you provide are applied. Changes are wrapped in an undo transaction. Use get_replication_info to inspect the current state before modifying."))
		.Idempotent()
		.StringArg(TEXT("actor_name"), TEXT("Label of the actor to configure replication settings on"), true)
		.BoolArg(TEXT("replicate"), TEXT("Enable or disable replication on the actor (bReplicates)"))
		.BoolArg(TEXT("replicate_movement"), TEXT("Enable or disable movement replication (bReplicateMovement)"))
		.BoolArg(TEXT("always_relevant"), TEXT("If true, this actor is always relevant to all clients (bAlwaysRelevant)"))
		.BoolArg(TEXT("only_relevant_to_owner"), TEXT("If true, only the owning client receives this actor's replication updates (bOnlyRelevantToOwner)"))
		.NumberArg(TEXT("net_update_frequency"), TEXT("How many times per second the actor checks for replication updates (NetUpdateFrequency). Typical range: 1-100."))
		.NumberArg(TEXT("min_net_update_frequency"), TEXT("Minimum update frequency when the actor is not moving or changing (MinNetUpdateFrequency). Must be <= net_update_frequency."))
		.NumberArg(TEXT("net_priority"), TEXT("Priority given to this actor when bandwidth is constrained. Higher value = sent first (NetPriority). Typical range: 1.0-5.0."))
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

			TArray<FString> Applied;
			bool bHasAnyParam = false;

			// Pre-check: at least one optional param must be present
			static const TArray<FString> KnownParams = {
				TEXT("replicate"), TEXT("replicate_movement"), TEXT("always_relevant"),
				TEXT("only_relevant_to_owner"), TEXT("net_update_frequency"),
				TEXT("min_net_update_frequency"), TEXT("net_priority")
			};
			for (const FString& P : KnownParams)
			{
				if (Args->HasField(P)) { bHasAnyParam = true; break; }
			}

			if (!bHasAnyParam)
			{
				return FMCPToolResult::Error(
					TEXT("No replication settings provided. Supply at least one of: replicate, replicate_movement, "
					     "always_relevant, only_relevant_to_owner, net_update_frequency, min_net_update_frequency, net_priority"));
			}

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Set Replication Settings")));
			Actor->Modify();

			bool bReplicate = false;
			if (Args->TryGetBoolField(TEXT("replicate"), bReplicate))
			{
				Actor->SetReplicates(bReplicate);
				Applied.Add(FString::Printf(TEXT("bReplicates=%s"), bReplicate ? TEXT("true") : TEXT("false")));
			}

			bool bRepMovement = false;
			if (Args->TryGetBoolField(TEXT("replicate_movement"), bRepMovement))
			{
				Actor->SetReplicateMovement(bRepMovement);
				Applied.Add(FString::Printf(TEXT("bReplicateMovement=%s"), bRepMovement ? TEXT("true") : TEXT("false")));
			}

			bool bAlwaysRelevant = false;
			if (Args->TryGetBoolField(TEXT("always_relevant"), bAlwaysRelevant))
			{
				Actor->bAlwaysRelevant = bAlwaysRelevant;
				Applied.Add(FString::Printf(TEXT("bAlwaysRelevant=%s"), bAlwaysRelevant ? TEXT("true") : TEXT("false")));
			}

			bool bOnlyOwner = false;
			if (Args->TryGetBoolField(TEXT("only_relevant_to_owner"), bOnlyOwner))
			{
				Actor->bOnlyRelevantToOwner = bOnlyOwner;
				Applied.Add(FString::Printf(TEXT("bOnlyRelevantToOwner=%s"), bOnlyOwner ? TEXT("true") : TEXT("false")));
			}

			if (Args->HasField(TEXT("net_update_frequency")))
			{
				float Freq = (float)Args->GetNumberField(TEXT("net_update_frequency"));
				Actor->SetNetUpdateFrequency(Freq);
				Applied.Add(FString::Printf(TEXT("NetUpdateFrequency=%.2f"), Freq));
			}

			if (Args->HasField(TEXT("min_net_update_frequency")))
			{
				float MinFreq = (float)Args->GetNumberField(TEXT("min_net_update_frequency"));
				Actor->SetMinNetUpdateFrequency(MinFreq);
				Applied.Add(FString::Printf(TEXT("MinNetUpdateFrequency=%.2f"), MinFreq));
			}

			if (Args->HasField(TEXT("net_priority")))
			{
				float Priority = (float)Args->GetNumberField(TEXT("net_priority"));
				Actor->NetPriority = Priority;
				Applied.Add(FString::Printf(TEXT("NetPriority=%.2f"), Priority));
			}

			Actor->PostEditChange();
			GEditor->EndTransaction();

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Replication settings updated on '%s': %s"),
				*ActorName, *FString::Join(Applied, TEXT(", "))));
		});

	// ================================================================
	// set_net_dormancy - Set the network dormancy mode on an actor
	// ================================================================
	MCP_TOOL(Registry, "set_net_dormancy")
		.Description(TEXT("Set the network dormancy mode on an actor to control how aggressively replication bandwidth is conserved. Dormant actors stop sending replication updates until manually woken with FlushNetDormancy. Use DORM_DormantAll for static or infrequently-updated actors to save bandwidth."))
		.Idempotent()
		.StringArg(TEXT("actor_name"), TEXT("Label of the actor to configure network dormancy on"), true)
		.EnumArg(TEXT("dormancy"), TEXT("Network dormancy mode. DORM_Never: always replicate. DORM_Awake: replicate while awake. "
			     "DORM_DormantAll: dormant for all connections (most bandwidth-efficient). "
			     "DORM_DormantPartial: dormant for some connections. "
			     "DORM_Initial: starts dormant until explicitly woken."), { TEXT("DORM_Never"), TEXT("DORM_Awake"), TEXT("DORM_DormantAll"), TEXT("DORM_DormantPartial"), TEXT("DORM_Initial") }, true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			FString ActorName;
			if (!Args->TryGetStringField(TEXT("actor_name"), ActorName))
				return FMCPToolResult::Error(TEXT("actor_name is required"));

			FString DormancyStr;
			if (!Args->TryGetStringField(TEXT("dormancy"), DormancyStr))
				return FMCPToolResult::Error(TEXT("dormancy is required"));

			AActor* Actor = FindActorByLabel(World, ActorName);
			if (!Actor)
				return FMCPToolResult::Error(FString::Printf(TEXT("Actor not found: %s"), *ActorName));

			ENetDormancy NewDormancy = DORM_Never; // overwritten below; init silences C4701
			bool bValidDormancy = true;
			if      (DormancyStr == TEXT("DORM_Never"))          NewDormancy = DORM_Never;
			else if (DormancyStr == TEXT("DORM_Awake"))          NewDormancy = DORM_Awake;
			else if (DormancyStr == TEXT("DORM_DormantAll"))     NewDormancy = DORM_DormantAll;
			else if (DormancyStr == TEXT("DORM_DormantPartial")) NewDormancy = DORM_DormantPartial;
			else if (DormancyStr == TEXT("DORM_Initial"))        NewDormancy = DORM_Initial;
			else                                                  bValidDormancy = false;

			if (!bValidDormancy)
			{
				return FMCPToolResult::Error(FString::Printf(
					TEXT("Invalid dormancy value: '%s'. Valid values: DORM_Never, DORM_Awake, DORM_DormantAll, DORM_DormantPartial, DORM_Initial"),
					*DormancyStr));
			}

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Set Net Dormancy")));
			Actor->Modify();

			Actor->NetDormancy = NewDormancy;

			Actor->PostEditChange();
			GEditor->EndTransaction();

			return FMCPToolResult::Success(FString::Printf(
				TEXT("NetDormancy set to '%s' on actor '%s'"),
				*DormancyStr, *ActorName));
		});

	// ================================================================
	// get_component_replication - Get replication info for all components
	// ================================================================
	MCP_TOOL(Registry, "get_component_replication")
		.Description(TEXT("Get a list of all components on an actor with their replication state. Reports: component name, class, bIsReplicated, and bReplicateUsingRegisteredSubObjectList for each component."))
		.ReadOnly()
		.Idempotent()
		.StringArg(TEXT("actor_name"), TEXT("Label of the actor whose components to inspect"), true)
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

			TArray<UActorComponent*> Components;
			Actor->GetComponents(Components);

			TArray<TSharedPtr<FJsonValue>> ComponentArray;
			for (UActorComponent* Comp : Components)
			{
				if (!Comp) continue;

				TSharedPtr<FJsonObject> CompObj = MakeShared<FJsonObject>();
				CompObj->SetStringField(TEXT("name"),  Comp->GetName());
				CompObj->SetStringField(TEXT("class"), Comp->GetClass()->GetName());
				CompObj->SetBoolField  (TEXT("bIsReplicated"),                          Comp->GetIsReplicated());
				CompObj->SetBoolField  (TEXT("bReplicateUsingRegisteredSubObjectList"), Comp->IsUsingRegisteredSubObjectList());

				ComponentArray.Add(MakeShared<FJsonValueObject>(CompObj));
			}

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("actor"),           ActorName);
			Result->SetNumberField(TEXT("component_count"), (double)ComponentArray.Num());
			Result->SetArrayField (TEXT("components"),      ComponentArray);

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});

	// ================================================================
	// set_component_replication - Enable/disable replication on a component
	// ================================================================
	MCP_TOOL(Registry, "set_component_replication")
		.Description(TEXT("Enable or disable network replication on a specific component of an actor. The actor must have replication enabled (bReplicates) for component replication to have any effect at runtime. Use get_component_replication to list component names."))
		.Idempotent()
		.StringArg(TEXT("actor_name"), TEXT("Label of the actor that owns the component"), true)
		.StringArg(TEXT("component_name"), TEXT("Name of the component to configure (use get_component_replication to list component names)"), true)
		.BoolArg(TEXT("replicate"), TEXT("True to enable replication on the component, false to disable it"), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			FString ActorName;
			if (!Args->TryGetStringField(TEXT("actor_name"), ActorName))
				return FMCPToolResult::Error(TEXT("actor_name is required"));

			FString ComponentName;
			if (!Args->TryGetStringField(TEXT("component_name"), ComponentName))
				return FMCPToolResult::Error(TEXT("component_name is required"));

			bool bReplicate = false;
			if (!Args->TryGetBoolField(TEXT("replicate"), bReplicate))
				return FMCPToolResult::Error(TEXT("replicate is required"));

			AActor* Actor = FindActorByLabel(World, ActorName);
			if (!Actor)
				return FMCPToolResult::Error(FString::Printf(TEXT("Actor not found: %s"), *ActorName));

			// Search for the named component
			TArray<UActorComponent*> Components;
			Actor->GetComponents(Components);

			UActorComponent* TargetComp = nullptr;
			for (UActorComponent* Comp : Components)
			{
				if (Comp && Comp->GetName() == ComponentName)
				{
					TargetComp = Comp;
					break;
				}
			}

			if (!TargetComp)
			{
				// Build a list of available component names to help the caller
				TArray<FString> Names;
				for (UActorComponent* Comp : Components)
				{
					if (Comp) Names.Add(Comp->GetName());
				}
				return FMCPToolResult::Error(FString::Printf(
					TEXT("Component '%s' not found on actor '%s'. Available components: [%s]"),
					*ComponentName, *ActorName, *FString::Join(Names, TEXT(", "))));
			}

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Set Component Replication")));
			TargetComp->Modify();

			TargetComp->SetIsReplicated(bReplicate);

			TargetComp->PostEditChange();
			GEditor->EndTransaction();

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Component '%s' on actor '%s': replication set to %s"),
				*ComponentName, *ActorName, bReplicate ? TEXT("enabled") : TEXT("disabled")));
		});

} // RegisterAll

} // namespace MCPNetworkingTools
