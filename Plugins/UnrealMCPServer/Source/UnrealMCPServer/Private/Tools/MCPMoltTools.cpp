// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "Tools/MCPMoltTools.h"

#include "MCPToolRegistry.h"
#include "MCPToolBuilder.h"
#include "MCPValidate.h"
#include "MCPProtocol.h"

#include "Common/MCPEditorContext.h"
#include "Common/MCPActorResolver.h"
#include "Common/MCPPropertyIO.h"

#include "Editor.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
#include "Kismet/GameplayStatics.h"

#include "Engine/Blueprint.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"

#include "Engine/SkeletalMesh.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMeshSocket.h"

#include "UObject/UnrealType.h"
#include "UObject/UObjectIterator.h"
#include "UObject/Class.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

// Tier 2 / Tier 3 additions
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/EngineTypes.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "MCPSettings.h"
#include "IPythonScriptPlugin.h"
#include "Modules/ModuleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"

namespace MCPMoltTools
{

// ----- helpers ---------------------------------------------------------------

static bool TryGetVector(const TSharedPtr<FJsonObject>& Args, const FString& X, const FString& Y,
	const FString& Z, FVector& Out, const FVector& Default)
{
	Out = Default;
	bool bAny = false;
	double V;
	if (Args->TryGetNumberField(X, V)) { Out.X = V; bAny = true; }
	if (Args->TryGetNumberField(Y, V)) { Out.Y = V; bAny = true; }
	if (Args->TryGetNumberField(Z, V)) { Out.Z = V; bAny = true; }
	return bAny;
}

static USkeleton* ResolveSkeleton(const FString& AssetPath, USkeletalMesh*& OutMesh)
{
	OutMesh = nullptr;
	UObject* Obj = LoadObject<UObject>(nullptr, *AssetPath);
	if (!Obj) return nullptr;
	if (USkeleton* Skel = Cast<USkeleton>(Obj))
	{
		return Skel;
	}
	if (USkeletalMesh* Mesh = Cast<USkeletalMesh>(Obj))
	{
		OutMesh = Mesh;
		return Mesh->GetSkeleton();
	}
	return nullptr;
}

static UBlueprint* LoadBP(const FString& Path)
{
	return LoadObject<UBlueprint>(nullptr, *Path);
}

static USCS_Node* FindSCSNode(UBlueprint* BP, const FString& CompName)
{
	if (!BP || !BP->SimpleConstructionScript) return nullptr;
	for (USCS_Node* Node : BP->SimpleConstructionScript->GetAllNodes())
	{
		if (Node && Node->GetVariableName().ToString() == CompName) return Node;
	}
	return nullptr;
}

static UEdGraph* FindGraphByName(UBlueprint* BP, const FString& GraphName)
{
	if (!BP) return nullptr;
	TArray<UEdGraph*> Graphs;
	BP->GetAllGraphs(Graphs);
	for (UEdGraph* G : Graphs)
	{
		if (G && G->GetName() == GraphName) return G;
	}
	return nullptr;
}

static UEdGraphNode* FindNodeByGuidStr(UEdGraph* Graph, const FString& Guid)
{
	if (!Graph) return nullptr;
	for (UEdGraphNode* N : Graph->Nodes)
	{
		if (N && N->NodeGuid.ToString().Equals(Guid, ESearchCase::IgnoreCase)) return N;
	}
	return nullptr;
}

static UClass* FindComponentClassByName(const FString& Name)
{
	UClass* C = FindFirstObject<UClass>(*Name, EFindFirstObjectOptions::ExactClass);
	if (!C) C = FindFirstObject<UClass>(*Name);
	return C;
}

// Run an embedded Python snippet through the engine's Python plugin (same path as
// execute_python). Returns false + a ready error result when unavailable.
static bool RunPython(const FString& Code, FMCPToolResult& OutErr)
{
	const UMCPSettings* S = UMCPSettings::Get();
	if (S && !S->bEnablePythonBridge)
	{
		OutErr = FMCPToolResult::Error(TEXT("Python bridge is disabled in Project Settings > Plugins > Unreal MCP Server."));
		return false;
	}
	IPythonScriptPlugin* Py = FModuleManager::GetModulePtr<IPythonScriptPlugin>(TEXT("PythonScriptPlugin"));
	if (!Py)
	{
		OutErr = FMCPToolResult::Error(TEXT("PythonScriptPlugin is not loaded. Enable it in the plugin manager."));
		return false;
	}
	Py->ExecPythonCommand(*Code);
	return true;
}

static FString RetargetResultPath()
{
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("mcp_retarget_result.json")).Replace(TEXT("\\"), TEXT("/"));
}

static TSharedPtr<FJsonObject> ReadJsonFile(const FString& Path)
{
	FString Content;
	if (!FFileHelper::LoadFileToString(Content, *Path)) return nullptr;
	TSharedPtr<FJsonObject> Obj;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Content);
	FJsonSerializer::Deserialize(Reader, Obj);
	return Obj;
}

// Split a content path "/Game/A/B/Name" into folder "/Game/A/B" and "Name".
static void SplitContentPath(const FString& Full, FString& OutFolder, FString& OutName)
{
	int32 Idx;
	if (Full.FindLastChar('/', Idx))
	{
		OutFolder = Full.Left(Idx);
		OutName = Full.Mid(Idx + 1);
	}
	else
	{
		OutFolder = TEXT("/Game");
		OutName = Full;
	}
}

// ----- registration ----------------------------------------------------------

void RegisterAll(FMCPToolRegistry& Registry)
{
	// =====================================================================
	// set_component_socket  (Blueprint)
	//   Set a Blueprint SCS component's parent attach socket / bone.
	//   The component must already be parented to a SkeletalMeshComponent
	//   (use add_component parent_component=... first).
	// =====================================================================
	Registry.SetActiveCategory(FName("Blueprint"));
	MCP_TOOL(Registry, "set_component_socket")
		.Description(TEXT("Set a Blueprint component's parent attach socket/bone (e.g. attach a Sword component to 'hand_r'). The component must already be a child of a SkeletalMeshComponent. Pass an empty socket_name to clear it. Requires the Blueprint to be recompiled (done automatically)."))
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Blueprint (e.g. /Game/_Game/Characters/Hero/BP_StraySparkCharacter)"), true)
		.StringArg(TEXT("component_name"), TEXT("Name of the component whose attach socket to set (e.g. 'Sword')"), true)
		.StringArg(TEXT("socket_name"), TEXT("Bone or socket name on the parent to attach to (e.g. 'hand_r'). Empty clears it."), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath, CompName, SocketName;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("asset_path"), AssetPath));
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("component_name"), CompName));
			Args->TryGetStringField(TEXT("socket_name"), SocketName);

			UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *AssetPath);
			if (!BP || !BP->SimpleConstructionScript)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
					FString::Printf(TEXT("Blueprint not found or has no construction script: %s"), *AssetPath),
					TEXT("Verify the content path points to a Blueprint asset."));
			}

			USCS_Node* Target = nullptr;
			for (USCS_Node* Node : BP->SimpleConstructionScript->GetAllNodes())
			{
				if (Node && Node->GetVariableName().ToString() == CompName)
				{
					Target = Node;
					break;
				}
			}
			if (!Target)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
					FString::Printf(TEXT("Component '%s' not found in %s"), *CompName, *AssetPath),
					TEXT("Use get_blueprint_info to list components. Note: only SCS (Blueprint-added) components can be re-socketed."));
			}

			Target->Modify();
			Target->AttachToName = SocketName.IsEmpty() ? NAME_None : FName(*SocketName);
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
			FKismetEditorUtilities::CompileBlueprint(BP);

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Set component '%s' parent socket to '%s' on %s (recompiled). Save the asset to persist."),
				*CompName, SocketName.IsEmpty() ? TEXT("(none)") : *SocketName, *AssetPath));
		});

	// =====================================================================
	// add_skeleton_socket  (Animation)
	// =====================================================================
	Registry.SetActiveCategory(FName("Animation"));
	MCP_TOOL(Registry, "add_skeleton_socket")
		.Description(TEXT("Add (or overwrite) a named socket on a Skeleton at a given bone, with an optional relative transform. Accepts a Skeleton or a SkeletalMesh content path. Mark the skeleton dirty; save to persist."))
		.StringArg(TEXT("asset_path"), TEXT("Content path of a Skeleton or SkeletalMesh"), true)
		.StringArg(TEXT("socket_name"), TEXT("Name for the new socket (e.g. 'SwordSocket')"), true)
		.StringArg(TEXT("bone_name"), TEXT("Bone the socket attaches to (e.g. 'hand_r')"), true)
		.NumberArg(TEXT("loc_x"), TEXT("Relative location X (default 0)"))
		.NumberArg(TEXT("loc_y"), TEXT("Relative location Y (default 0)"))
		.NumberArg(TEXT("loc_z"), TEXT("Relative location Z (default 0)"))
		.NumberArg(TEXT("rot_pitch"), TEXT("Relative rotation Pitch (default 0)"))
		.NumberArg(TEXT("rot_yaw"), TEXT("Relative rotation Yaw (default 0)"))
		.NumberArg(TEXT("rot_roll"), TEXT("Relative rotation Roll (default 0)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath, SocketName, BoneName;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("asset_path"), AssetPath));
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("socket_name"), SocketName));
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("bone_name"), BoneName));

			USkeletalMesh* Mesh = nullptr;
			USkeleton* Skeleton = ResolveSkeleton(AssetPath, Mesh);
			if (!Skeleton)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
					FString::Printf(TEXT("Could not resolve a Skeleton from: %s"), *AssetPath),
					TEXT("Pass a Skeleton or SkeletalMesh content path."));
			}

			if (Skeleton->GetReferenceSkeleton().FindBoneIndex(FName(*BoneName)) == INDEX_NONE)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
					FString::Printf(TEXT("Bone '%s' not found on skeleton '%s'"), *BoneName, *Skeleton->GetName()),
					TEXT("Use get_skeleton_info to list bone names."));
			}

			FVector Loc; TryGetVector(Args, TEXT("loc_x"), TEXT("loc_y"), TEXT("loc_z"), Loc, FVector::ZeroVector);
			double P = 0, Y = 0, R = 0;
			Args->TryGetNumberField(TEXT("rot_pitch"), P);
			Args->TryGetNumberField(TEXT("rot_yaw"), Y);
			Args->TryGetNumberField(TEXT("rot_roll"), R);

			Skeleton->Modify();
			// Overwrite an existing socket of the same name for idempotency.
			for (int32 i = Skeleton->Sockets.Num() - 1; i >= 0; --i)
			{
				USkeletalMeshSocket* Existing = Skeleton->Sockets[i];
				if (Existing && Existing->SocketName == FName(*SocketName))
				{
					Skeleton->Sockets.RemoveAt(i);
				}
			}

			USkeletalMeshSocket* Socket = NewObject<USkeletalMeshSocket>(Skeleton);
			Socket->SocketName = FName(*SocketName);
			Socket->BoneName = FName(*BoneName);
			Socket->RelativeLocation = Loc;
			Socket->RelativeRotation = FRotator(P, Y, R);
			Socket->RelativeScale = FVector::OneVector;
			Skeleton->Sockets.Add(Socket);
			Skeleton->MarkPackageDirty();

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Added socket '%s' on bone '%s' (skeleton '%s'). Save the skeleton to persist."),
				*SocketName, *BoneName, *Skeleton->GetName()));
		});

	// =====================================================================
	// list_skeleton_sockets  (Animation)
	// =====================================================================
	Registry.SetActiveCategory(FName("Animation"));
	MCP_TOOL(Registry, "list_skeleton_sockets")
		.Description(TEXT("List sockets on a Skeleton (and mesh-only sockets if a SkeletalMesh path is given): name, bone, source."))
		.ReadOnly()
		.Idempotent()
		.StringArg(TEXT("asset_path"), TEXT("Content path of a Skeleton or SkeletalMesh"), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("asset_path"), AssetPath));

			USkeletalMesh* Mesh = nullptr;
			USkeleton* Skeleton = ResolveSkeleton(AssetPath, Mesh);
			if (!Skeleton)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
					FString::Printf(TEXT("Could not resolve a Skeleton from: %s"), *AssetPath));
			}

			TArray<TSharedPtr<FJsonValue>> Arr;
			auto AddSocket = [&Arr](const USkeletalMeshSocket* S, const TCHAR* Source)
			{
				if (!S) return;
				TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
				O->SetStringField(TEXT("name"), S->SocketName.ToString());
				O->SetStringField(TEXT("bone"), S->BoneName.ToString());
				O->SetStringField(TEXT("source"), Source);
				Arr.Add(MakeShared<FJsonValueObject>(O));
			};
			for (const USkeletalMeshSocket* S : Skeleton->Sockets) { AddSocket(S, TEXT("skeleton")); }
			if (Mesh)
			{
				for (const USkeletalMeshSocket* S : Mesh->GetMeshOnlySocketList()) { AddSocket(S, TEXT("mesh")); }
			}

			TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
			Out->SetStringField(TEXT("skeleton"), Skeleton->GetName());
			Out->SetNumberField(TEXT("count"), Arr.Num());
			Out->SetArrayField(TEXT("sockets"), Arr);
			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("%d socket(s) on '%s'."), Arr.Num(), *Skeleton->GetName()), Out);
		});

	// =====================================================================
	// pie_get_actor_property  (PIE)
	//   Read LIVE property values from the running PIE world (the editor-world
	//   inspectors can't see PIE state).
	// =====================================================================
	Registry.SetActiveCategory(FName("PIE"));
	MCP_TOOL(Registry, "pie_get_actor_property")
		.Description(TEXT("Read live property values from an actor (or one of its components) in the RUNNING PIE world. Defaults to player 0's pawn. Use 'component' to read a component (e.g. 'Health') instead of the actor. Omit property_names for all editable/blueprint-visible properties."))
		.ReadOnly()
		.StringArg(TEXT("actor_name"), TEXT("Actor label to read; if omitted, uses the player pawn"))
		.IntArg(TEXT("player_index"), TEXT("Player index when reading the player pawn (default 0)"))
		.StringArg(TEXT("component"), TEXT("Read this component (name substring match) instead of the actor, e.g. 'Health'"))
		.StringArrayArg(TEXT("property_names"), TEXT("Specific properties to read; omit for all editable ones"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			if (!GEditor || !GEditor->IsPlaySessionInProgress() || !GEditor->PlayWorld)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::Internal,
					TEXT("No PIE session running."), TEXT("Start Play-In-Editor first (pie_start)."));
			}
			UWorld* World = GEditor->PlayWorld;

			AActor* Actor = nullptr;
			FString ActorName;
			if (Args->TryGetStringField(TEXT("actor_name"), ActorName) && !ActorName.IsEmpty())
			{
				Actor = MCPCommon::FindActorByLabel(World, ActorName);
				if (!Actor)
				{
					return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
						FString::Printf(TEXT("Actor '%s' not found in PIE world."), *ActorName));
				}
			}
			else
			{
				int32 PlayerIndex = 0;
				if (Args->HasField(TEXT("player_index")))
				{
					PlayerIndex = (int32)Args->GetNumberField(TEXT("player_index"));
				}
				Actor = UGameplayStatics::GetPlayerPawn(World, PlayerIndex);
				if (!Actor)
				{
					return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
						FString::Printf(TEXT("No player pawn for index %d in PIE world."), PlayerIndex));
				}
			}

			UObject* Target = Actor;
			FString CompName;
			if (Args->TryGetStringField(TEXT("component"), CompName) && !CompName.IsEmpty())
			{
				UActorComponent* Found = nullptr;
				TArray<UActorComponent*> Comps;
				Actor->GetComponents(Comps);
				for (UActorComponent* C : Comps)
				{
					if (C && (C->GetName().Contains(CompName) || C->GetClass()->GetName().Contains(CompName)))
					{
						Found = C;
						break;
					}
				}
				if (!Found)
				{
					return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
						FString::Printf(TEXT("Component matching '%s' not found on '%s'."), *CompName, *Actor->GetActorLabel()),
						TEXT("Use list_actor_components to see component names."));
				}
				Target = Found;
			}

			TArray<FString> Requested;
			const TArray<TSharedPtr<FJsonValue>>* RawNames = nullptr;
			if (Args->TryGetArrayField(TEXT("property_names"), RawNames) && RawNames)
			{
				for (const TSharedPtr<FJsonValue>& V : *RawNames)
				{
					FString S;
					if (V.IsValid() && V->TryGetString(S)) { Requested.Add(S); }
				}
			}

			TSharedPtr<FJsonObject> Props = MakeShared<FJsonObject>();
			for (TFieldIterator<FProperty> It(Target->GetClass()); It; ++It)
			{
				FProperty* Prop = *It;
				if (!Prop) continue;
				if (Requested.Num() == 0 && !Prop->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible)) continue;
				const FString PropName = Prop->GetName();
				if (Requested.Num() > 0 && !Requested.Contains(PropName)) continue;
				TSharedPtr<FJsonValue> Val = MCPCommon::ExportPropertyToJson(Prop, Target);
				Props->SetField(PropName, Val.IsValid() ? Val : MakeShared<FJsonValueNull>());
			}

			TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
			Out->SetStringField(TEXT("actor"), Actor->GetActorLabel());
			Out->SetStringField(TEXT("target_class"), Target->GetClass()->GetName());
			Out->SetObjectField(TEXT("properties"), Props);
			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Read %d propert(ies) from '%s' (%s)."),
					Props->Values.Num(), *Actor->GetActorLabel(), *Target->GetClass()->GetName()), Out);
		});

	// =====================================================================
	// TIER 2
	// =====================================================================

	// ----- set_component_object_property (Blueprint) ---------------------
	Registry.SetActiveCategory(FName("Blueprint"));
	MCP_TOOL(Registry, "set_component_object_property")
		.Description(TEXT("Set an object- or class-reference property on a Blueprint component by asset path (e.g. WidgetComponent.WidgetClass, StaticMeshComponent.StaticMesh). For class properties (TSubclassOf), pass a Blueprint/asset path and its generated class is used. Recompiles the Blueprint."))
		.StringArg(TEXT("asset_path"), TEXT("Blueprint content path"), true)
		.StringArg(TEXT("component_name"), TEXT("Component name (e.g. 'HealthBarWidget')"), true)
		.StringArg(TEXT("property_name"), TEXT("Property to set (e.g. 'WidgetClass', 'StaticMesh')"), true)
		.StringArg(TEXT("value_asset_path"), TEXT("Content path of the asset/Blueprint to assign"), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath, CompName, PropName, ValuePath;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("asset_path"), AssetPath));
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("component_name"), CompName));
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("property_name"), PropName));
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("value_asset_path"), ValuePath));

			UBlueprint* BP = LoadBP(AssetPath);
			USCS_Node* Node = FindSCSNode(BP, CompName);
			if (!Node || !Node->ComponentTemplate)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
					FString::Printf(TEXT("Component '%s' not found in %s"), *CompName, *AssetPath));
			}
			UActorComponent* Template = Node->ComponentTemplate;
			FProperty* Prop = Template->GetClass()->FindPropertyByName(FName(*PropName));
			if (!Prop)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
					FString::Printf(TEXT("Property '%s' not found on %s"), *PropName, *Template->GetClass()->GetName()));
			}
			UObject* AssetObj = LoadObject<UObject>(nullptr, *ValuePath);
			if (!AssetObj)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
					FString::Printf(TEXT("Value asset not found: %s"), *ValuePath));
			}

			auto AsClass = [AssetObj]() -> UClass*
			{
				if (UClass* C = Cast<UClass>(AssetObj)) return C;
				if (UBlueprint* VB = Cast<UBlueprint>(AssetObj)) return VB->GeneratedClass;
				return nullptr;
			};

			Template->Modify();
			if (FClassProperty* CP = CastField<FClassProperty>(Prop))
			{
				UClass* C = AsClass();
				if (!C) return FMCPToolResult::ErrorStructured(EMCPError::Unsupported, TEXT("value is not a class/Blueprint for a class property"));
				CP->SetObjectPropertyValue_InContainer(Template, C);
			}
			else if (FSoftClassProperty* SCP = CastField<FSoftClassProperty>(Prop))
			{
				UClass* C = AsClass();
				if (!C) return FMCPToolResult::ErrorStructured(EMCPError::Unsupported, TEXT("value is not a class/Blueprint for a soft class property"));
				SCP->SetObjectPropertyValue_InContainer(Template, C);
			}
			else if (FSoftObjectProperty* SOP = CastField<FSoftObjectProperty>(Prop))
			{
				SOP->SetObjectPropertyValue_InContainer(Template, AssetObj);
			}
			else if (FObjectProperty* OP = CastField<FObjectProperty>(Prop))
			{
				OP->SetObjectPropertyValue_InContainer(Template, AssetObj);
			}
			else
			{
				return FMCPToolResult::ErrorStructured(EMCPError::Unsupported,
					TEXT("Property is not an object/class reference; use set_component_property for value types."));
			}

			FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
			FKismetEditorUtilities::CompileBlueprint(BP);
			return FMCPToolResult::Success(FString::Printf(
				TEXT("Set %s.%s = %s (recompiled). Save the asset to persist."), *CompName, *PropName, *ValuePath));
		});

	// ----- set_component_collision (Physics) ----------------------------
	Registry.SetActiveCategory(FName("Physics"));
	MCP_TOOL(Registry, "set_component_collision")
		.Description(TEXT("Set collision profile / enabled / overlap-events on a NAMED Blueprint component (works on sub-components, unlike set_collision_profile which only targets the actor root). Recompiles."))
		.StringArg(TEXT("asset_path"), TEXT("Blueprint content path"), true)
		.StringArg(TEXT("component_name"), TEXT("Primitive component name (e.g. 'Trigger')"), true)
		.StringArg(TEXT("profile_name"), TEXT("Collision profile (e.g. 'OverlapAllDynamic', 'BlockAll', 'NoCollision')"))
		.EnumArg(TEXT("collision_enabled"), TEXT("Collision enabled mode"), { TEXT("NoCollision"), TEXT("QueryOnly"), TEXT("PhysicsOnly"), TEXT("QueryAndPhysics") })
		.BoolArg(TEXT("generate_overlap_events"), TEXT("Whether the component generates overlap events"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath, CompName;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("asset_path"), AssetPath));
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("component_name"), CompName));

			UBlueprint* BP = LoadBP(AssetPath);
			USCS_Node* Node = FindSCSNode(BP, CompName);
			if (!Node || !Node->ComponentTemplate)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
					FString::Printf(TEXT("Component '%s' not found in %s"), *CompName, *AssetPath));
			}
			UPrimitiveComponent* Prim = Cast<UPrimitiveComponent>(Node->ComponentTemplate);
			if (!Prim)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::Unsupported,
					FString::Printf(TEXT("Component '%s' is not a PrimitiveComponent"), *CompName));
			}

			Prim->Modify();
			FString Profile;
			if (Args->TryGetStringField(TEXT("profile_name"), Profile) && !Profile.IsEmpty())
			{
				Prim->SetCollisionProfileName(FName(*Profile));
			}
			FString CE;
			if (Args->TryGetStringField(TEXT("collision_enabled"), CE) && !CE.IsEmpty())
			{
				ECollisionEnabled::Type E = ECollisionEnabled::QueryAndPhysics;
				if (CE == TEXT("NoCollision")) E = ECollisionEnabled::NoCollision;
				else if (CE == TEXT("QueryOnly")) E = ECollisionEnabled::QueryOnly;
				else if (CE == TEXT("PhysicsOnly")) E = ECollisionEnabled::PhysicsOnly;
				Prim->SetCollisionEnabled(E);
			}
			bool bOverlap = false;
			if (Args->TryGetBoolField(TEXT("generate_overlap_events"), bOverlap))
			{
				Prim->SetGenerateOverlapEvents(bOverlap);
			}

			FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
			FKismetEditorUtilities::CompileBlueprint(BP);
			return FMCPToolResult::Success(FString::Printf(
				TEXT("Updated collision on '%s' in %s (recompiled). Save to persist."), *CompName, *AssetPath));
		});

	// ----- set_node_enabled (Blueprint) ---------------------------------
	Registry.SetActiveCategory(FName("Blueprint"));
	MCP_TOOL(Registry, "set_node_enabled")
		.Description(TEXT("Enable, disable, or set development-only state on a Blueprint graph node by GUID. Recompiles."))
		.StringArg(TEXT("asset_path"), TEXT("Blueprint content path"), true)
		.StringArg(TEXT("graph_name"), TEXT("Graph name (default 'EventGraph')"))
		.StringArg(TEXT("node_id"), TEXT("GUID of the node (from describe_graph)"), true)
		.EnumArg(TEXT("state"), TEXT("New enabled state"), { TEXT("Enabled"), TEXT("Disabled"), TEXT("DevelopmentOnly") }, true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath, NodeId, State;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("asset_path"), AssetPath));
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("node_id"), NodeId));
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("state"), State));
			FString GraphName = TEXT("EventGraph");
			Args->TryGetStringField(TEXT("graph_name"), GraphName);

			UBlueprint* BP = LoadBP(AssetPath);
			if (!BP) return FMCPToolResult::ErrorStructured(EMCPError::NotFound, FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
			UEdGraph* Graph = FindGraphByName(BP, GraphName);
			if (!Graph) return FMCPToolResult::ErrorStructured(EMCPError::NotFound, FString::Printf(TEXT("Graph '%s' not found"), *GraphName));
			UEdGraphNode* Node = FindNodeByGuidStr(Graph, NodeId);
			if (!Node) return FMCPToolResult::ErrorStructured(EMCPError::NotFound, FString::Printf(TEXT("Node '%s' not found in '%s'"), *NodeId, *GraphName));

			ENodeEnabledState ES = ENodeEnabledState::Enabled;
			if (State == TEXT("Disabled")) ES = ENodeEnabledState::Disabled;
			else if (State == TEXT("DevelopmentOnly")) ES = ENodeEnabledState::DevelopmentOnly;
			Node->SetEnabledState(ES, true);

			FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
			FKismetEditorUtilities::CompileBlueprint(BP);
			return FMCPToolResult::Success(FString::Printf(TEXT("Set node '%s' to %s (recompiled)."), *NodeId, *State));
		});

	// ----- add_child_component (Blueprint) ------------------------------
	Registry.SetActiveCategory(FName("Blueprint"));
	MCP_TOOL(Registry, "add_child_component")
		.Description(TEXT("Add a component to a Blueprint, parented to ANY component including an INHERITED native one (e.g. CharacterMesh0) which the basic add_component cannot do. Optional attach socket, static mesh, and widget class. Recompiles."))
		.StringArg(TEXT("asset_path"), TEXT("Blueprint content path"), true)
		.StringArg(TEXT("component_class"), TEXT("Component class name (e.g. 'StaticMeshComponent', 'WidgetComponent')"), true)
		.StringArg(TEXT("component_name"), TEXT("Name for the new component"), true)
		.StringArg(TEXT("parent_component"), TEXT("Parent component name (SCS or inherited native, e.g. 'CharacterMesh0')"), true)
		.StringArg(TEXT("socket_name"), TEXT("Optional bone/socket on the parent to attach to (e.g. 'hand_r')"))
		.StringArg(TEXT("static_mesh"), TEXT("Optional StaticMesh asset path (for StaticMeshComponent)"))
		.StringArg(TEXT("widget_class"), TEXT("Optional UserWidget Blueprint path (for WidgetComponent)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath, CompClassName, CompName, ParentName;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("asset_path"), AssetPath));
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("component_class"), CompClassName));
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("component_name"), CompName));
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("parent_component"), ParentName));

			UBlueprint* BP = LoadBP(AssetPath);
			if (!BP || !BP->SimpleConstructionScript)
				return FMCPToolResult::ErrorStructured(EMCPError::NotFound, FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));

			UClass* CompClass = FindComponentClassByName(CompClassName);
			if (!CompClass || !CompClass->IsChildOf(UActorComponent::StaticClass()))
				return FMCPToolResult::ErrorStructured(EMCPError::NotFound, FString::Printf(TEXT("Component class not found: %s"), *CompClassName));
			if (FindSCSNode(BP, CompName))
				return FMCPToolResult::ErrorStructured(EMCPError::AlreadyExists, FString::Printf(TEXT("A component named '%s' already exists"), *CompName));

			USCS_Node* NewNode = BP->SimpleConstructionScript->CreateNode(CompClass, FName(*CompName));
			if (!NewNode) return FMCPToolResult::Error(TEXT("Failed to create SCS node"));

			if (USCS_Node* ParentNode = FindSCSNode(BP, ParentName))
			{
				ParentNode->AddChildNode(NewNode);
			}
			else
			{
				USceneComponent* NativeParent = nullptr;
				if (UClass* GC = BP->GeneratedClass)
				{
					if (AActor* CDO = Cast<AActor>(GC->GetDefaultObject()))
					{
						TArray<UActorComponent*> Comps;
						CDO->GetComponents(Comps);
						for (UActorComponent* C : Comps)
						{
							if (C && C->GetName() == ParentName) { NativeParent = Cast<USceneComponent>(C); break; }
						}
					}
				}
				if (!NativeParent)
				{
					return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
						FString::Printf(TEXT("Parent component '%s' not found (neither SCS nor inherited native)"), *ParentName));
				}
				NewNode->SetParent(NativeParent);
				BP->SimpleConstructionScript->AddNode(NewNode);
			}

			FString Socket;
			if (Args->TryGetStringField(TEXT("socket_name"), Socket) && !Socket.IsEmpty())
			{
				NewNode->AttachToName = FName(*Socket);
			}

			UActorComponent* Template = NewNode->ComponentTemplate;
			FString MeshPath;
			if (Template && Args->TryGetStringField(TEXT("static_mesh"), MeshPath) && !MeshPath.IsEmpty())
			{
				if (UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(Template))
				{
					if (UStaticMesh* M = LoadObject<UStaticMesh>(nullptr, *MeshPath)) { SMC->SetStaticMesh(M); }
				}
			}
			FString WClassPath;
			if (Template && Args->TryGetStringField(TEXT("widget_class"), WClassPath) && !WClassPath.IsEmpty())
			{
				if (FClassProperty* CP = CastField<FClassProperty>(Template->GetClass()->FindPropertyByName(FName(TEXT("WidgetClass")))))
				{
					UObject* A = LoadObject<UObject>(nullptr, *WClassPath);
					UClass* WC = Cast<UClass>(A);
					if (!WC) { if (UBlueprint* VB = Cast<UBlueprint>(A)) WC = VB->GeneratedClass; }
					if (WC) { CP->SetObjectPropertyValue_InContainer(Template, WC); }
				}
			}

			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
			FKismetEditorUtilities::CompileBlueprint(BP);
			return FMCPToolResult::Success(FString::Printf(
				TEXT("Added '%s' (%s) under '%s'%s in %s (recompiled). Save to persist."),
				*CompName, *CompClassName, *ParentName,
				Socket.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" @socket '%s'"), *Socket), *AssetPath));
		});

	// ----- call_function (Actor) ----------------------------------------
	Registry.SetActiveCategory(FName("Actor"));
	MCP_TOOL(Registry, "call_function")
        .Destructive() // General code execution can exceed Scene effects.
		.Description(TEXT("Call a BlueprintCallable/native UFunction on an actor (or one of its components) with JSON args and return any output/return values. Works in the editor world or the running PIE world. Replaces many execute_python calls (e.g. read GetHealthPercent, call Heal/TakeDamage)."))
		.StringArg(TEXT("function_name"), TEXT("UFunction name to call (e.g. 'GetHealthPercent')"), true)
		.StringArg(TEXT("actor_name"), TEXT("Actor label; omit to use the player pawn"))
		.IntArg(TEXT("player_index"), TEXT("Player index when using the player pawn (default 0)"))
		.StringArg(TEXT("component"), TEXT("Target this component (name/class substring) instead of the actor, e.g. 'Health'"))
		.ObjectArg(TEXT("args"), TEXT("Function arguments as a JSON object {paramName: value}"), MakeShared<FJsonObject>())
		.EnumArg(TEXT("world"), TEXT("Which world to run in"), { TEXT("editor"), TEXT("pie") })
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString FuncName;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("function_name"), FuncName));
			FString WorldSel = TEXT("editor");
			Args->TryGetStringField(TEXT("world"), WorldSel);

			UWorld* World = nullptr;
			if (WorldSel == TEXT("pie"))
			{
				World = (GEditor && GEditor->IsPlaySessionInProgress()) ? GEditor->PlayWorld : nullptr;
				if (!World) return FMCPToolResult::ErrorStructured(EMCPError::Internal, TEXT("No PIE session running."), TEXT("Use pie_start, or set world='editor'."));
			}
			else
			{
				World = MCPCommon::GetEditorWorld();
				if (!World) return FMCPToolResult::ErrorStructured(EMCPError::Internal, TEXT("No editor world available."));
			}

			AActor* Actor = nullptr;
			FString ActorName;
			if (Args->TryGetStringField(TEXT("actor_name"), ActorName) && !ActorName.IsEmpty())
			{
				Actor = MCPCommon::FindActorByLabel(World, ActorName);
				if (!Actor) return FMCPToolResult::ErrorStructured(EMCPError::NotFound, FString::Printf(TEXT("Actor '%s' not found."), *ActorName));
			}
			else
			{
				int32 PlayerIndex = 0;
				if (Args->HasField(TEXT("player_index"))) PlayerIndex = (int32)Args->GetNumberField(TEXT("player_index"));
				Actor = UGameplayStatics::GetPlayerPawn(World, PlayerIndex);
				if (!Actor) return FMCPToolResult::ErrorStructured(EMCPError::NotFound, FString::Printf(TEXT("No player pawn for index %d."), PlayerIndex));
			}

			UObject* Target = Actor;
			FString CompName;
			if (Args->TryGetStringField(TEXT("component"), CompName) && !CompName.IsEmpty())
			{
				TArray<UActorComponent*> Comps;
				Actor->GetComponents(Comps);
				UActorComponent* Found = nullptr;
				for (UActorComponent* C : Comps)
				{
					if (C && (C->GetName().Contains(CompName) || C->GetClass()->GetName().Contains(CompName))) { Found = C; break; }
				}
				if (!Found) return FMCPToolResult::ErrorStructured(EMCPError::NotFound, FString::Printf(TEXT("Component '%s' not found on '%s'."), *CompName, *Actor->GetActorLabel()));
				Target = Found;
			}

			UFunction* Func = Target->FindFunction(FName(*FuncName));
			if (!Func) return FMCPToolResult::ErrorStructured(EMCPError::NotFound, FString::Printf(TEXT("Function '%s' not found on %s."), *FuncName, *Target->GetClass()->GetName()));

			uint8* Parms = (uint8*)FMemory_Alloca(FMath::Max<int32>(Func->ParmsSize, 1));
			FMemory::Memzero(Parms, Func->ParmsSize);
			for (TFieldIterator<FProperty> It(Func); It; ++It)
			{
				FProperty* P = *It;
				if (P->HasAnyPropertyFlags(CPF_Parm)) { P->InitializeValue_InContainer(Parms); }
			}

			const TSharedPtr<FJsonObject>* ArgsObj = nullptr;
			if (Args->TryGetObjectField(TEXT("args"), ArgsObj) && ArgsObj && ArgsObj->IsValid())
			{
				for (TFieldIterator<FProperty> It(Func); It; ++It)
				{
					FProperty* P = *It;
					if (!P->HasAnyPropertyFlags(CPF_Parm) || P->HasAnyPropertyFlags(CPF_ReturnParm)) continue;
					// v4.5 (5.8): FJsonObject keys are UE::FSharedString; use the
					// FStringView-based accessor instead of Values.Find(FString).
					TSharedPtr<FJsonValue> V = (*ArgsObj)->TryGetField(P->GetName());
					if (V.IsValid())
					{
						FString Err;
						MCPCommon::SetPropertyFromJson(P, Parms, Target, V, Err);
					}
				}
			}

			Target->ProcessEvent(Func, Parms);

			TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
			for (TFieldIterator<FProperty> It(Func); It; ++It)
			{
				FProperty* P = *It;
				if (!P->HasAnyPropertyFlags(CPF_Parm)) continue;
				const bool bRet = P->HasAnyPropertyFlags(CPF_ReturnParm);
				const bool bOut = P->HasAnyPropertyFlags(CPF_OutParm) && !P->HasAnyPropertyFlags(CPF_ConstParm);
				if (bRet || bOut)
				{
					TSharedPtr<FJsonValue> Jv = MCPCommon::ExportPropertyToJson(P, Parms);
					Out->SetField(bRet ? FString(TEXT("return")) : P->GetName(), Jv.IsValid() ? Jv : MakeShared<FJsonValueNull>());
				}
			}
			for (TFieldIterator<FProperty> It(Func); It; ++It)
			{
				FProperty* P = *It;
				if (P->HasAnyPropertyFlags(CPF_Parm)) { P->DestroyValue_InContainer(Parms); }
			}

			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Called %s on %s."), *FuncName, *Target->GetClass()->GetName()), Out);
		});

	// =====================================================================
	// TIER 3 — Retarget (wraps the proven Python pipeline; see ANIM_RETARGET_GUIDE.md)
	// =====================================================================

	// ----- create_ik_rig (Animation) ------------------------------------
	Registry.SetActiveCategory(FName("Animation"));
	MCP_TOOL(Registry, "create_ik_rig")
		.Description(TEXT("Create an IK Rig from a skeletal mesh with auto-generated retarget chains. Optionally drops the UE5 finger metacarpal chains (recommended when retargeting from UE4 sources to avoid finger claw)."))
		.StringArg(TEXT("mesh_path"), TEXT("SkeletalMesh content path"), true)
		.StringArg(TEXT("output_path"), TEXT("Output IK Rig content path (e.g. /Game/_Game/Animations/Retarget/IK_Hero)"), true)
		.BoolArg(TEXT("drop_finger_metacarpals"), TEXT("Remove the 8 metacarpal chains (default true)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString MeshPath, OutPath;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("mesh_path"), MeshPath));
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("output_path"), OutPath));
			bool bDrop = true; Args->TryGetBoolField(TEXT("drop_finger_metacarpals"), bDrop);
			FString Folder, Name; SplitContentPath(OutPath, Folder, Name);
			const FString ResFile = RetargetResultPath();

			const FString Py = FString::Printf(TEXT(
				"import unreal, json\n"
				"try:\n"
				"    at=unreal.AssetToolsHelpers.get_asset_tools()\n"
				"    rig=at.create_asset('%s','%s',unreal.IKRigDefinition,unreal.IKRigDefinitionFactory())\n"
				"    rc=unreal.IKRigController.get_controller(rig)\n"
				"    rc.set_skeletal_mesh(unreal.load_asset('%s'))\n"
				"    rc.apply_auto_generated_retarget_definition()\n"
				"    if %s:\n"
				"        for c in [s+f+'Metacarpal' for s in ['Left','Right'] for f in ['Index','Middle','Ring','Pinky']]:\n"
				"            try: rc.remove_retarget_chain(c)\n"
				"            except Exception: pass\n"
				"    unreal.EditorAssetLibrary.save_asset('%s',only_if_is_dirty=False)\n"
				"    res=dict(ok=True,path='%s',chains=len(rc.get_retarget_chains()))\n"
				"except Exception as e:\n"
				"    res=dict(ok=False,error=str(e)[:300])\n"
				"open('%s','w').write(json.dumps(res))\n"),
				*Name, *Folder, *MeshPath, bDrop ? TEXT("True") : TEXT("False"), *OutPath, *OutPath, *ResFile);

			FMCPToolResult Err;
			if (!RunPython(Py, Err)) return Err;
			TSharedPtr<FJsonObject> R = ReadJsonFile(ResFile);
			if (!R) return FMCPToolResult::Error(TEXT("IK Rig script produced no result."));
			bool bOk = false; R->TryGetBoolField(TEXT("ok"), bOk);
			if (!bOk)
			{
				FString E; R->TryGetStringField(TEXT("error"), E);
				return FMCPToolResult::ErrorStructured(EMCPError::Internal, FString::Printf(TEXT("create_ik_rig failed: %s"), *E));
			}
			return FMCPToolResult::SuccessStructured(FString::Printf(TEXT("Created IK Rig %s."), *OutPath), R);
		});

	// ----- create_ik_retargeter (Animation) -----------------------------
	Registry.SetActiveCategory(FName("Animation"));
	MCP_TOOL(Registry, "create_ik_retargeter")
		.Description(TEXT("Create an IK Retargeter linking a source and target IK Rig: sets source/target, assigns rigs to all retarget ops (required, or output is a static T-pose), and fuzzy-maps chains."))
		.StringArg(TEXT("source_ik_rig"), TEXT("Source IK Rig content path"), true)
		.StringArg(TEXT("target_ik_rig"), TEXT("Target IK Rig content path"), true)
		.StringArg(TEXT("output_path"), TEXT("Output IK Retargeter content path"), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString Src, Tgt, OutPath;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("source_ik_rig"), Src));
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("target_ik_rig"), Tgt));
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("output_path"), OutPath));
			FString Folder, Name; SplitContentPath(OutPath, Folder, Name);
			const FString ResFile = RetargetResultPath();

			const FString Py = FString::Printf(TEXT(
				"import unreal, json\n"
				"try:\n"
				"    at=unreal.AssetToolsHelpers.get_asset_tools()\n"
				"    src=unreal.load_asset('%s'); tgt=unreal.load_asset('%s')\n"
				"    rt=at.create_asset('%s','%s',unreal.IKRetargeter,unreal.IKRetargetFactory())\n"
				"    rtc=unreal.IKRetargeterController.get_controller(rt)\n"
				"    rtc.set_ik_rig(unreal.RetargetSourceOrTarget.SOURCE,src)\n"
				"    rtc.set_ik_rig(unreal.RetargetSourceOrTarget.TARGET,tgt)\n"
				"    rtc.assign_ik_rig_to_all_ops(unreal.RetargetSourceOrTarget.SOURCE,src)\n"
				"    rtc.assign_ik_rig_to_all_ops(unreal.RetargetSourceOrTarget.TARGET,tgt)\n"
				"    rtc.auto_map_chains(unreal.AutoMapChainType.FUZZY,True)\n"
				"    unreal.EditorAssetLibrary.save_asset('%s',only_if_is_dirty=False)\n"
				"    res=dict(ok=True,path='%s')\n"
				"except Exception as e:\n"
				"    res=dict(ok=False,error=str(e)[:300])\n"
				"open('%s','w').write(json.dumps(res))\n"),
				*Src, *Tgt, *Name, *Folder, *OutPath, *OutPath, *ResFile);

			FMCPToolResult Err;
			if (!RunPython(Py, Err)) return Err;
			TSharedPtr<FJsonObject> R = ReadJsonFile(ResFile);
			if (!R) return FMCPToolResult::Error(TEXT("Retargeter script produced no result."));
			bool bOk = false; R->TryGetBoolField(TEXT("ok"), bOk);
			if (!bOk)
			{
				FString E; R->TryGetStringField(TEXT("error"), E);
				return FMCPToolResult::ErrorStructured(EMCPError::Internal, FString::Printf(TEXT("create_ik_retargeter failed: %s"), *E));
			}
			return FMCPToolResult::SuccessStructured(FString::Printf(TEXT("Created IK Retargeter %s."), *OutPath), R);
		});

	// ----- retarget_animations (Animation) ------------------------------
	Registry.SetActiveCategory(FName("Animation"));
	MCP_TOOL(Registry, "retarget_animations")
		.Description(TEXT("Batch-retarget animation sequences through an IK Retargeter and move the results into an output folder. Handles the duplicate_and_retarget + relocation gotchas automatically."))
		.StringArg(TEXT("source_mesh"), TEXT("Source SkeletalMesh content path"), true)
		.StringArg(TEXT("target_mesh"), TEXT("Target SkeletalMesh content path"), true)
		.StringArg(TEXT("retargeter"), TEXT("IK Retargeter content path"), true)
		.StringArrayArg(TEXT("anim_paths"), TEXT("Source AnimSequence content paths to retarget"), true)
		.StringArg(TEXT("output_folder"), TEXT("Destination folder for the retargeted anims (e.g. /Game/_Game/Characters/Hero/Anims/Sword)"), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString SrcMesh, TgtMesh, Retargeter, OutFolder;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("source_mesh"), SrcMesh));
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("target_mesh"), TgtMesh));
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("retargeter"), Retargeter));
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("output_folder"), OutFolder));

			FString PyList = TEXT("[");
			const TArray<TSharedPtr<FJsonValue>>* Raw = nullptr;
			if (Args->TryGetArrayField(TEXT("anim_paths"), Raw) && Raw)
			{
				for (const TSharedPtr<FJsonValue>& V : *Raw)
				{
					FString S;
					if (V.IsValid() && V->TryGetString(S)) { PyList += TEXT("'") + S + TEXT("',"); }
				}
			}
			PyList += TEXT("]");
			if (PyList == TEXT("[]")) return FMCPToolResult::ErrorStructured(EMCPError::OutOfRange, TEXT("anim_paths is empty."));
			const FString ResFile = RetargetResultPath();

			const FString Py = FString::Printf(TEXT(
				"import unreal, json\n"
				"try:\n"
				"    src=unreal.load_asset('%s'); tgt=unreal.load_asset('%s'); rt=unreal.load_asset('%s')\n"
				"    names=%s\n"
				"    dest='%s'\n"
				"    if not dest.endswith('/'): dest=dest+'/'\n"
				"    assets=[unreal.EditorAssetLibrary.find_asset_data(p) for p in names]\n"
				"    batch=unreal.IKRetargetBatchOperation()\n"
				"    batch.duplicate_and_retarget(assets,src,tgt,rt,search='',replace='',prefix='',suffix='',include_referenced_assets=False,overwrite_existing_files=True)\n"
				"    out=[]\n"
				"    for p in names:\n"
				"        base=p.rsplit('/',1)[-1]\n"
				"        rootp='/Game/'+base\n"
				"        if unreal.EditorAssetLibrary.does_asset_exist(rootp):\n"
				"            unreal.EditorAssetLibrary.rename_asset(rootp, dest+base)\n"
				"            out.append(dest+base)\n"
				"    res=dict(ok=True,retargeted=out,count=len(out))\n"
				"except Exception as e:\n"
				"    res=dict(ok=False,error=str(e)[:300])\n"
				"open('%s','w').write(json.dumps(res))\n"),
				*SrcMesh, *TgtMesh, *Retargeter, *PyList, *OutFolder, *ResFile);

			FMCPToolResult Err;
			if (!RunPython(Py, Err)) return Err;
			TSharedPtr<FJsonObject> R = ReadJsonFile(ResFile);
			if (!R) return FMCPToolResult::Error(TEXT("Retarget script produced no result."));
			bool bOk = false; R->TryGetBoolField(TEXT("ok"), bOk);
			if (!bOk)
			{
				FString E; R->TryGetStringField(TEXT("error"), E);
				return FMCPToolResult::ErrorStructured(EMCPError::Internal, FString::Printf(TEXT("retarget_animations failed: %s"), *E));
			}
			int32 Count = 0; R->TryGetNumberField(TEXT("count"), Count);
			return FMCPToolResult::SuccessStructured(FString::Printf(TEXT("Retargeted %d animation(s) to %s."), Count, *OutFolder), R);
		});
}

} // namespace MCPMoltTools
