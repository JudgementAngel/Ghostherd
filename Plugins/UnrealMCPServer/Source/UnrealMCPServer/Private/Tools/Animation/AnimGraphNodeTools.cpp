// Copyright StraySpark Studio 2026. All Rights Reserved.
//
// v4.6 Phase 3 — AnimGraph node authoring and state machine completion.
//
// This file closes the three defects that made the v4.5 state-machine tools
// produce assets that compiled but did nothing:
//
//   D1  add_anim_state created a state whose BoundGraph held only the schema's
//       default Result node. Nothing put a pose into it, so every state
//       evaluated to reference pose. set_anim_state_animation fixes that.
//
//   D2  add_anim_transition hand-wired pins and never authored the rule graph,
//       leaving bCanEnterTransition at its default of false — the transition
//       could never be taken. set_anim_transition_rule fixes that.
//
//   D3  No tool could add a node to an AnimGraph, so a state machine could
//       never be connected to the Output Pose node. animgraph_add_node and
//       animgraph_connect_pose fix that.
//
// Node creation always goes through FGraphNodeCreator, which is what the
// engine's own UAnimationStateGraphSchema::CreateDefaultNodesForGraph uses. Do
// not hand-roll NewObject + PostPlacedNewNode here: several anim node classes
// allocate their bound graphs inside PostPlacedNewNode behind a
// check(BoundGraph == NULL), and the ordering is easy to get wrong.

#include "Tools/Animation/AnimCommon.h"
#include "Common/MCPAssetResolver.h"
#include "Common/MCPGraphSerializer.h"
#include "Common/MCPPropertyIO.h"

#include "MCPToolRegistry.h"
#include "MCPToolBuilder.h"
#include "MCPProtocol.h"
#include "MCPValidate.h"

#include "Animation/AnimBlueprint.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimationAsset.h"
#include "Animation/BlendProfile.h"
#include "Animation/Skeleton.h"

#include "AnimGraphNode_AssetPlayerBase.h"
#include "AnimGraphNode_Base.h"
#include "AnimGraphNode_BlendSpacePlayer.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_SequencePlayer.h"
#include "AnimGraphNode_StateMachine.h"
#include "AnimGraphNode_StateResult.h"
#include "AnimGraphNode_TransitionResult.h"
#include "AnimStateEntryNode.h"
#include "AnimStateNode.h"
#include "AnimStateTransitionNode.h"
#include "AnimationGraph.h"
#include "AnimationGraphSchema.h"
#include "AnimationStateGraph.h"
#include "AnimationStateMachineGraph.h"
#include "AnimationTransitionGraph.h"

#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "K2Node_VariableGet.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UObject/UObjectIterator.h"

namespace MCPAnimTools::AnimGraphNodes
{

using namespace MCPAnimTools::Common;

// =====================================================================
// Local helpers
// =====================================================================

/** Load an AnimBlueprint by its asset_path arg. */
static UAnimBlueprint* LoadAnimBP(const TSharedPtr<FJsonObject>& Args, FMCPToolResult& OutError)
{
	FString Path;
	FMCPValidateResult Check = FMCPValidate::RequiredString(Args, TEXT("asset_path"), Path);
	if (!Check.bOk)
	{
		OutError = Check.Error;
		return nullptr;
	}
	return MCPCommon::LoadAssetChecked<UAnimBlueprint>(Path, OutError);
}

/** Create an anim graph node of a runtime-resolved class, engine-canonically.
 *  Configure runs between CreateNode and Finalize — i.e. before
 *  PostPlacedNewNode/AllocateDefaultPins — which is where node identity (the
 *  played asset, the slot name) must be set so the right pins get allocated. */
static UAnimGraphNode_Base* CreateAnimNode(UEdGraph* Graph, UClass* NodeClass,
	int32 PosX, int32 PosY, TFunctionRef<void(UAnimGraphNode_Base*)> Configure)
{
	FGraphNodeCreator<UAnimGraphNode_Base> Creator(*Graph);
	UAnimGraphNode_Base* Node = Creator.CreateNode(/*bSelectNewNode*/ false, NodeClass);
	if (!Node)
	{
		return nullptr;
	}

	Configure(Node);
	Node->NodePosX = PosX;
	Node->NodePosY = PosY;
	Creator.Finalize();
	return Node;
}

/** Find a graph's sole node of a given type. */
template <typename T>
static T* FindNodeOfType(UEdGraph* Graph)
{
	if (!IsValid(Graph))
	{
		return nullptr;
	}
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (T* Typed = Cast<T>(Node))
		{
			return Typed;
		}
	}
	return nullptr;
}

/** Resolve a node inside a graph by its GUID string. */
static UEdGraphNode* FindNodeByGuid(UEdGraph* Graph, const FString& GuidString, FMCPToolResult& OutError)
{
	FGuid Guid;
	if (!FGuid::Parse(GuidString, Guid))
	{
		OutError = FMCPToolResult::ErrorStructured(EMCPError::InvalidName,
			FString::Printf(TEXT("'%s' is not a valid node id (expected a GUID)."), *GuidString),
			TEXT("Node ids come from animgraph_add_node or animgraph_describe."));
		return nullptr;
	}

	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (IsValid(Node) && Node->NodeGuid == Guid)
		{
			return Node;
		}
	}

	OutError = FMCPToolResult::ErrorStructured(EMCPError::NotFound,
		FString::Printf(TEXT("No node with id '%s' in graph '%s'."), *GuidString, *Graph->GetName()),
		TEXT("Call animgraph_describe to list the graph's nodes and their ids."));
	return nullptr;
}

/** Resolve the AnimGraph, or a named state's inner graph, from the tool args.
 *  Every node tool accepts an optional `state_name` so the same tools can
 *  author the top-level graph and the inside of a state. */
static UEdGraph* ResolveTargetGraph(UAnimBlueprint* AnimBP, const TSharedPtr<FJsonObject>& Args,
	FString& OutDescription, FMCPToolResult& OutError)
{
	FString StateName, MachineName;
	Args->TryGetStringField(TEXT("state_name"), StateName);
	Args->TryGetStringField(TEXT("machine_name"), MachineName);

	if (StateName.IsEmpty())
	{
		UEdGraph* AnimGraph = FindAnimGraph(AnimBP);
		if (!AnimGraph)
		{
			OutError = FMCPToolResult::ErrorStructured(EMCPError::NotFound,
				FString::Printf(TEXT("AnimBlueprint '%s' has no AnimGraph."), *AnimBP->GetName()),
				TEXT("The asset may be corrupt, or not actually an Animation Blueprint."));
			return nullptr;
		}
		OutDescription = FString::Printf(TEXT("AnimGraph of '%s'"), *AnimBP->GetName());
		return AnimGraph;
	}

	if (MachineName.IsEmpty())
	{
		OutError = FMCPToolResult::ErrorStructured(EMCPError::OutOfRange,
			TEXT("machine_name is required when state_name is given."),
			TEXT("State names are only unique within one state machine."));
		return nullptr;
	}

	UAnimationStateMachineGraph* SMGraph = FindStateMachineGraph(AnimBP, MachineName, OutError);
	if (!SMGraph) return nullptr;

	UAnimStateNode* State = FindState(SMGraph, StateName, OutError);
	if (!State) return nullptr;

	if (!State->BoundGraph)
	{
		OutError = FMCPToolResult::ErrorStructured(EMCPError::Internal,
			FString::Printf(TEXT("State '%s' has no bound graph."), *StateName));
		return nullptr;
	}

	OutDescription = FString::Printf(TEXT("state '%s' in machine '%s'"), *StateName, *MachineName);
	return State->BoundGraph;
}

/** Wire an output pose pin to an input pose pin, replacing any existing link on
 *  the input (a pose input takes exactly one source). */
static bool ConnectPose(UEdGraphPin* From, UEdGraphPin* To, FMCPToolResult& OutError)
{
	if (!From || !To)
	{
		OutError = FMCPToolResult::ErrorStructured(EMCPError::Internal, TEXT("Missing pose pin."));
		return false;
	}

	To->BreakAllPinLinks();
	From->MakeLinkTo(To);
	return true;
}

/** Mark the blueprint dirty after a structural graph edit. */
static void MarkAnimBPModified(UAnimBlueprint* AnimBP)
{
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
	AnimBP->MarkPackageDirty();
}

// =====================================================================

void RegisterAll(FMCPToolRegistry& Registry)
{
	// =================================================================
	// animgraph_list_node_types
	// =================================================================
	MCP_TOOL(Registry, "animgraph_list_node_types")
		.Description(TEXT(
			"List the AnimGraph node classes available to animgraph_add_node, with their menu category and "
			"pose pins. The engine ships ~130 of them — sequence and blend space players, blends "
			"(LayeredBoneBlend, ApplyAdditive, BlendListByBool), the Slot node that montages play into, "
			"skeletal controls (TwoBoneIK, ModifyBone), Inertialization, Mirror, cached poses, and linked "
			"anim layers. Filter with the `filter` argument to keep the response small."))
		.ReadOnly()
		.Idempotent()
		.StringArg(TEXT("filter"), TEXT("Case-insensitive substring to match against the class name (e.g. 'Blend', 'Slot', 'IK')"))
		.IntArg(TEXT("limit"), TEXT("Maximum classes to return (default: 60, max: 300)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));

			FString Filter;
			Args->TryGetStringField(TEXT("filter"), Filter);

			int32 Limit = 60;
			if (Args->HasField(TEXT("limit")))
			{
				Limit = FMath::Clamp((int32)Args->GetNumberField(TEXT("limit")), 1, 300);
			}

			TArray<UClass*> Classes;
			for (TObjectIterator<UClass> It; It; ++It)
			{
				UClass* Candidate = *It;
				if (!Candidate->IsChildOf(UAnimGraphNode_Base::StaticClass())) continue;
				if (Candidate->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)) continue;
				if (!Filter.IsEmpty() && !Candidate->GetName().Contains(Filter, ESearchCase::IgnoreCase)) continue;
				Classes.Add(Candidate);
			}

			Classes.Sort([](const UClass& A, const UClass& B) { return A.GetName() < B.GetName(); });
			const int32 TotalMatching = Classes.Num();

			TArray<TSharedPtr<FJsonValue>> Entries;
			for (UClass* Candidate : Classes)
			{
				if (Entries.Num() >= Limit) break;

				TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
				Obj->SetStringField(TEXT("class"), Candidate->GetName());
				Obj->SetStringField(TEXT("class_path"), Candidate->GetPathName());

				if (const UAnimGraphNode_Base* CDO = Cast<UAnimGraphNode_Base>(Candidate->GetDefaultObject()))
				{
					Obj->SetStringField(TEXT("category"), CDO->GetMenuCategory().ToString());
				}
				Obj->SetBoolField(TEXT("is_asset_player"), Candidate->IsChildOf(UAnimGraphNode_AssetPlayerBase::StaticClass()));

				Entries.Add(MakeShared<FJsonValueObject>(Obj));
			}

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetNumberField(TEXT("count"), Entries.Num());
			Result->SetNumberField(TEXT("total_matching"), TotalMatching);
			Result->SetArrayField(TEXT("node_types"), Entries);
			if (!Filter.IsEmpty())
			{
				Result->SetStringField(TEXT("filter"), Filter);
			}
			if (TotalMatching > Entries.Num())
			{
				Result->SetStringField(TEXT("note"),
					FString::Printf(TEXT("Showing %d of %d. Narrow with `filter` or raise `limit`."),
						Entries.Num(), TotalMatching));
			}

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});

	// =================================================================
	// animgraph_describe
	// =================================================================
	MCP_TOOL(Registry, "animgraph_describe")
		.Description(TEXT(
			"Read back an Animation Blueprint's graph topology in one call: every node with its id, class, "
			"title, position and pins, plus a deduplicated edge list. Pass state_name + machine_name to "
			"describe the inside of a state instead of the top-level AnimGraph. This is the verify half of "
			"AnimGraph authoring — the node ids it returns are what animgraph_connect_pose and "
			"animgraph_set_node_property take."))
		.ReadOnly()
		.Idempotent()
		.StringArg(TEXT("asset_path"), TEXT("Content path to the UAnimBlueprint"), true)
		.StringArg(TEXT("state_name"), TEXT("Optional: describe this state's inner graph instead of the AnimGraph"))
		.StringArg(TEXT("machine_name"), TEXT("State machine containing state_name (required when state_name is given)"))
		.BoolArg(TEXT("include_hidden_pins"), TEXT("Include pins hidden in the editor (default: false)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			FMCPToolResult Err;
			UAnimBlueprint* AnimBP = LoadAnimBP(Args, Err);
			if (!AnimBP) return Err;

			FString Description;
			UEdGraph* Graph = ResolveTargetGraph(AnimBP, Args, Description, Err);
			if (!Graph) return Err;

			bool bHidden = false;
			Args->TryGetBoolField(TEXT("include_hidden_pins"), bHidden);

			TSharedPtr<FJsonObject> Result = MCPCommon::GraphToJson(Graph, /*bIncludePins*/ true, bHidden);
			Result->SetStringField(TEXT("anim_blueprint"), AnimBP->GetName());
			Result->SetStringField(TEXT("anim_blueprint_path"), AnimBP->GetPathName());
			Result->SetStringField(TEXT("graph"), Description);
			Result->SetBoolField(TEXT("is_compiled"), AnimBP->GetAnimBlueprintGeneratedClass() != nullptr);

			if (USkeleton* Skeleton = AnimBP->TargetSkeleton)
			{
				Result->SetStringField(TEXT("target_skeleton"), Skeleton->GetPathName());
			}

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});

	// =================================================================
	// animgraph_add_node
	// =================================================================
	MCP_TOOL(Registry, "animgraph_add_node")
		.Description(TEXT(
			"Add a node to an Animation Blueprint's AnimGraph (or, with state_name + machine_name, to the "
			"inside of a state). Takes any UAnimGraphNode_* class by name — use animgraph_list_node_types to "
			"discover them. For asset-player nodes (SequencePlayer, BlendSpacePlayer) pass animation_path and "
			"the asset is assigned during construction so the correct pins are allocated. The node is created "
			"unconnected; wire it with animgraph_connect_pose. Returns the new node's id.\n"
			"Adding a node does NOT compile the Blueprint — call compile_anim_blueprint when the graph is "
			"complete."))
		.SupportsDryRun()
		.StringArg(TEXT("asset_path"), TEXT("Content path to the UAnimBlueprint"), true)
		.StringArg(TEXT("node_class"), TEXT("AnimGraph node class, e.g. 'AnimGraphNode_SequencePlayer', 'AnimGraphNode_Slot', 'AnimGraphNode_LayeredBoneBlend'"), true)
		.StringArg(TEXT("animation_path"), TEXT("For asset-player nodes: the UAnimSequence or UBlendSpace to play. Must share the AnimBP's skeleton."))
		.StringArg(TEXT("state_name"), TEXT("Optional: add the node inside this state instead of the AnimGraph"))
		.StringArg(TEXT("machine_name"), TEXT("State machine containing state_name (required when state_name is given)"))
		.NumberArg(TEXT("position_x"), TEXT("Node X position in the graph (default: 0)"))
		.NumberArg(TEXT("position_y"), TEXT("Node Y position in the graph (default: 0)"))
		.Example(TEXT("{\"asset_path\": \"/Game/Characters/ABP_Hero\", \"node_class\": \"AnimGraphNode_Slot\", \"position_x\": 400}"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			FMCPToolResult Err;
			UAnimBlueprint* AnimBP = LoadAnimBP(Args, Err);
			if (!AnimBP) return Err;

			FString NodeClassName;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("node_class"), NodeClassName));

			UClass* NodeClass = ResolveClass(NodeClassName, UAnimGraphNode_Base::StaticClass(), Err);
			if (!NodeClass) return Err;
			if (NodeClass->HasAnyClassFlags(CLASS_Abstract))
			{
				return FMCPToolResult::ErrorStructured(EMCPError::InvalidName,
					FString::Printf(TEXT("'%s' is abstract and cannot be placed."), *NodeClass->GetName()),
					TEXT("Use animgraph_list_node_types to find a concrete node class."));
			}

			FString Description;
			UEdGraph* Graph = ResolveTargetGraph(AnimBP, Args, Description, Err);
			if (!Graph) return Err;

			// Resolve the played asset before construction so it can be applied
			// in the Configure step (pins depend on it).
			UAnimationAsset* AnimAsset = nullptr;
			FString AnimPath;
			if (Args->TryGetStringField(TEXT("animation_path"), AnimPath) && !AnimPath.IsEmpty())
			{
				AnimAsset = MCPCommon::LoadAssetChecked<UAnimationAsset>(AnimPath, Err);
				if (!AnimAsset) return Err;

				if (!NodeClass->IsChildOf(UAnimGraphNode_AssetPlayerBase::StaticClass()))
				{
					return FMCPToolResult::ErrorStructured(EMCPError::Unsupported,
						FString::Printf(TEXT("'%s' is not an asset-player node, so it cannot play '%s'."),
							*NodeClass->GetName(), *AnimAsset->GetName()),
						TEXT("Drop animation_path, or use AnimGraphNode_SequencePlayer / AnimGraphNode_BlendSpacePlayer."));
				}
				if (!RequireSameSkeleton(AnimBP->TargetSkeleton, AnimAsset, TEXT("animation_path"), Err)) return Err;
			}

			const int32 PosX = Args->HasField(TEXT("position_x")) ? (int32)Args->GetNumberField(TEXT("position_x")) : 0;
			const int32 PosY = Args->HasField(TEXT("position_y")) ? (int32)Args->GetNumberField(TEXT("position_y")) : 0;

			Graph->Modify();

			UAnimGraphNode_Base* NewNode = CreateAnimNode(Graph, NodeClass, PosX, PosY,
				[AnimAsset](UAnimGraphNode_Base* Node)
				{
					if (AnimAsset)
					{
						if (UAnimGraphNode_AssetPlayerBase* Player = Cast<UAnimGraphNode_AssetPlayerBase>(Node))
						{
							Player->SetAnimationAsset(AnimAsset);
						}
					}
				});

			if (!NewNode)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::Internal,
					FString::Printf(TEXT("Failed to create a '%s' node."), *NodeClass->GetName()));
			}

			MarkAnimBPModified(AnimBP);

			TSharedPtr<FJsonObject> Result = MCPCommon::NodeToJson(NewNode, /*bIncludePins*/ true);
			Result->SetStringField(TEXT("anim_blueprint"), AnimBP->GetPathName());
			Result->SetStringField(TEXT("graph"), Description);
			Result->SetStringField(TEXT("node_id"), NewNode->NodeGuid.ToString());
			Result->SetStringField(TEXT("pose_pins"), DescribePosePins(NewNode));

			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Added %s to the %s. Node id: %s. %s. Wire it with animgraph_connect_pose, then compile_anim_blueprint."),
					*NodeClass->GetName(), *Description, *NewNode->NodeGuid.ToString(), *DescribePosePins(NewNode)),
				Result);
		});

	// =================================================================
	// animgraph_connect_pose
	// =================================================================
	MCP_TOOL(Registry, "animgraph_connect_pose")
		.Description(TEXT(
			"Connect one AnimGraph node's output pose to another's input pose. Node ids come from "
			"animgraph_add_node or animgraph_describe; pass 'root' as to_node to connect into the graph's "
			"Output Pose node (or, inside a state, its Result node) — which is the connection that makes "
			"the graph actually evaluate anything.\n"
			"A pose input accepts exactly one source, so connecting replaces whatever was there. Nodes with "
			"several pose inputs (LayeredBoneBlend's BasePose and BlendPoses_0, ApplyAdditive's Base and "
			"Additive) need to_pin to disambiguate — animgraph_describe lists the pin names."))
		.SupportsDryRun()
		.StringArg(TEXT("asset_path"), TEXT("Content path to the UAnimBlueprint"), true)
		.StringArg(TEXT("from_node"), TEXT("Node id whose output pose is the source"), true)
		.StringArg(TEXT("to_node"), TEXT("Node id to connect into, or 'root' for the graph's Output Pose / Result node"), true)
		.StringArg(TEXT("to_pin"), TEXT("Name of the destination pose pin, when the target has more than one (e.g. 'BasePose', 'Additive')"))
		.StringArg(TEXT("from_pin"), TEXT("Name of the source pose pin, when the source has more than one"))
		.StringArg(TEXT("state_name"), TEXT("Optional: operate inside this state's graph instead of the AnimGraph"))
		.StringArg(TEXT("machine_name"), TEXT("State machine containing state_name (required when state_name is given)"))
		.Example(TEXT("{\"asset_path\": \"/Game/Characters/ABP_Hero\", \"from_node\": \"A1B2C3D4E5F6...\", \"to_node\": \"root\"}"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			FMCPToolResult Err;
			UAnimBlueprint* AnimBP = LoadAnimBP(Args, Err);
			if (!AnimBP) return Err;

			FString Description;
			UEdGraph* Graph = ResolveTargetGraph(AnimBP, Args, Description, Err);
			if (!Graph) return Err;

			FString FromId, ToId;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("from_node"), FromId));
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("to_node"), ToId));

			UEdGraphNode* FromNode = FindNodeByGuid(Graph, FromId, Err);
			if (!FromNode) return Err;

			// "root" resolves to whichever sink this graph type owns.
			UEdGraphNode* ToNode = nullptr;
			if (ToId.Equals(TEXT("root"), ESearchCase::IgnoreCase))
			{
				if (UAnimationStateGraph* StateGraph = Cast<UAnimationStateGraph>(Graph))
				{
					ToNode = StateGraph->GetResultNode();
				}
				else
				{
					ToNode = FindNodeOfType<UAnimGraphNode_Root>(Graph);
				}

				if (!ToNode)
				{
					return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
						FString::Printf(TEXT("The %s has no Output Pose / Result node."), *Description));
				}
			}
			else
			{
				ToNode = FindNodeByGuid(Graph, ToId, Err);
				if (!ToNode) return Err;
			}

			if (FromNode == ToNode)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::OutOfRange,
					TEXT("from_node and to_node are the same node."));
			}

			FString FromPinName, ToPinName;
			Args->TryGetStringField(TEXT("from_pin"), FromPinName);
			Args->TryGetStringField(TEXT("to_pin"), ToPinName);

			UEdGraphPin* FromPin = FindPosePin(FromNode, EGPD_Output, FromPinName);
			if (!FromPin)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
					FromPinName.IsEmpty()
						? FString::Printf(TEXT("Node '%s' has no output pose pin."), *FromNode->GetNodeTitle(ENodeTitleType::ListView).ToString())
						: FString::Printf(TEXT("Node '%s' has no output pose pin named '%s'."),
							*FromNode->GetNodeTitle(ENodeTitleType::ListView).ToString(), *FromPinName),
					DescribePosePins(FromNode));
			}

			UEdGraphPin* ToPin = FindPosePin(ToNode, EGPD_Input, ToPinName);
			if (!ToPin)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
					ToPinName.IsEmpty()
						? FString::Printf(TEXT("Node '%s' has no input pose pin."), *ToNode->GetNodeTitle(ENodeTitleType::ListView).ToString())
						: FString::Printf(TEXT("Node '%s' has no input pose pin named '%s'."),
							*ToNode->GetNodeTitle(ENodeTitleType::ListView).ToString(), *ToPinName),
					DescribePosePins(ToNode));
			}

			// Report what we are about to displace — a silently replaced link is
			// the kind of thing that makes an agent think its earlier edit worked.
			FString Replaced;
			if (ToPin->LinkedTo.Num() > 0 && ToPin->LinkedTo[0] && ToPin->LinkedTo[0]->GetOwningNode())
			{
				Replaced = ToPin->LinkedTo[0]->GetOwningNode()->GetNodeTitle(ENodeTitleType::ListView).ToString();
			}

			Graph->Modify();
			FromNode->Modify();
			ToNode->Modify();

			if (!ConnectPose(FromPin, ToPin, Err)) return Err;

			MarkAnimBPModified(AnimBP);

			TSharedPtr<FJsonObject> Result = MCPCommon::GraphToJson(Graph, /*bIncludePins*/ false);
			Result->SetStringField(TEXT("from_node"), FromNode->NodeGuid.ToString());
			Result->SetStringField(TEXT("to_node"), ToNode->NodeGuid.ToString());
			Result->SetStringField(TEXT("from_pin"), FromPin->PinName.ToString());
			Result->SetStringField(TEXT("to_pin"), ToPin->PinName.ToString());
			if (!Replaced.IsEmpty())
			{
				Result->SetStringField(TEXT("replaced_connection_from"), Replaced);
			}

			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Connected '%s'.%s -> '%s'.%s in the %s.%s"),
					*FromNode->GetNodeTitle(ENodeTitleType::ListView).ToString(), *FromPin->PinName.ToString(),
					*ToNode->GetNodeTitle(ENodeTitleType::ListView).ToString(), *ToPin->PinName.ToString(),
					*Description,
					Replaced.IsEmpty()
						? TEXT("")
						: *FString::Printf(TEXT(" This replaced an existing connection from '%s'."), *Replaced)),
				Result);
		});

	// =================================================================
	// animgraph_set_node_property
	// =================================================================
	MCP_TOOL(Registry, "animgraph_set_node_property")
		.Description(TEXT(
			"Set a property on an AnimGraph node. Most anim node settings live on the node's inner `Node` "
			"struct (a Slot node's SlotName, a SequencePlayer's PlayRate and bLoopAnimation, a "
			"LayeredBoneBlend's LayerSetup), so this tool looks the property up on the node object first and "
			"then inside that struct — pass the bare name, e.g. 'SlotName', not 'Node.SlotName'. Accepts "
			"both UE text syntax and native JSON values. Use animgraph_describe to see what a node exposes."))
		.SupportsDryRun()
		.StringArg(TEXT("asset_path"), TEXT("Content path to the UAnimBlueprint"), true)
		.StringArg(TEXT("node_id"), TEXT("Node id from animgraph_add_node or animgraph_describe"), true)
		.StringArg(TEXT("property_name"), TEXT("Property name, e.g. 'SlotName', 'PlayRate', 'bLoopAnimation'"), true)
		.StringArg(TEXT("value"), TEXT("New value. UE text syntax or JSON, e.g. 'UpperBody', '1.5', 'true'."), true)
		.StringArg(TEXT("state_name"), TEXT("Optional: the node lives inside this state's graph"))
		.StringArg(TEXT("machine_name"), TEXT("State machine containing state_name (required when state_name is given)"))
		.Example(TEXT("{\"asset_path\": \"/Game/Characters/ABP_Hero\", \"node_id\": \"...\", \"property_name\": \"SlotName\", \"value\": \"UpperBody\"}"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			FMCPToolResult Err;
			UAnimBlueprint* AnimBP = LoadAnimBP(Args, Err);
			if (!AnimBP) return Err;

			FString Description;
			UEdGraph* Graph = ResolveTargetGraph(AnimBP, Args, Description, Err);
			if (!Graph) return Err;

			FString NodeId, PropertyName, Value;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("node_id"), NodeId));
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("property_name"), PropertyName));
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("value"), Value));

			UEdGraphNode* Node = FindNodeByGuid(Graph, NodeId, Err);
			if (!Node) return Err;

			UAnimGraphNode_Base* AnimNode = Cast<UAnimGraphNode_Base>(Node);
			if (!AnimNode)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::Unsupported,
					FString::Printf(TEXT("Node '%s' is not an AnimGraph node."),
						*Node->GetNodeTitle(ENodeTitleType::ListView).ToString()));
			}

			AnimNode->Modify();

			FString SetError;
			bool bSet = false;
			FString Where;

			// 1) Directly on the node object.
			if (FProperty* Prop = AnimNode->GetClass()->FindPropertyByName(FName(*PropertyName)))
			{
				bSet = MCPCommon::SetObjectPropertyWithNotify(Prop, AnimNode, Value, SetError);
				Where = TEXT("node");
			}
			else
			{
				// 2) Inside the node's inner FAnimNode_* struct, where nearly all
				//    anim settings actually live.
				for (TFieldIterator<FStructProperty> It(AnimNode->GetClass()); It; ++It)
				{
					FStructProperty* StructProp = *It;
					if (!StructProp->Struct) continue;

					FProperty* Inner = StructProp->Struct->FindPropertyByName(FName(*PropertyName));
					if (!Inner) continue;

					void* StructMemory = StructProp->ContainerPtrToValuePtr<void>(AnimNode);
					bSet = MCPCommon::SetPropertyFromString(Inner, StructMemory, AnimNode, Value, SetError);
					Where = StructProp->GetName();
					break;
				}
			}

			if (Where.IsEmpty())
			{
				// Build a hint listing what the node does expose.
				TArray<FString> Available;
				for (TFieldIterator<FProperty> It(AnimNode->GetClass()); It && Available.Num() < 30; ++It)
				{
					if (It->HasAnyPropertyFlags(CPF_Edit)) { Available.Add(It->GetName()); }
				}
				for (TFieldIterator<FStructProperty> It(AnimNode->GetClass()); It; ++It)
				{
					if (!It->Struct) continue;
					for (TFieldIterator<FProperty> Inner(It->Struct); Inner && Available.Num() < 60; ++Inner)
					{
						if (Inner->HasAnyPropertyFlags(CPF_Edit)) { Available.Add(Inner->GetName()); }
					}
				}

				return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
					FString::Printf(TEXT("Node '%s' (%s) has no property named '%s'."),
						*AnimNode->GetNodeTitle(ENodeTitleType::ListView).ToString(),
						*AnimNode->GetClass()->GetName(), *PropertyName),
					TEXT("Pass the bare property name; the tool searches the node and its inner anim node struct."),
					Available);
			}

			if (!bSet)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::OutOfRange,
					FString::Printf(TEXT("Could not set '%s' on '%s': %s"),
						*PropertyName, *AnimNode->GetClass()->GetName(), *SetError));
			}

			// Pin visibility and titles can depend on the value just written.
			AnimNode->ReconstructNode();
			MarkAnimBPModified(AnimBP);

			TSharedPtr<FJsonObject> Result = MCPCommon::NodeToJson(AnimNode, /*bIncludePins*/ true);
			Result->SetStringField(TEXT("property_name"), PropertyName);
			Result->SetStringField(TEXT("value"), Value);
			Result->SetStringField(TEXT("set_on"), Where);

			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Set %s = '%s' on '%s' (via %s)."),
					*PropertyName, *Value, *AnimNode->GetClass()->GetName(), *Where),
				Result);
		});

	// =================================================================
	// set_anim_state_animation   (fixes D1)
	// =================================================================
	MCP_TOOL(Registry, "set_anim_state_animation")
		.Description(TEXT(
			"Put an animation into a state machine state and wire it to the state's Result node. This is the "
			"step that makes a state DO something: add_anim_state creates a state whose graph contains only "
			"an empty Result node, and a state left that way evaluates to reference pose (the character "
			"T-poses in that state). Accepts an AnimSequence (creates a Sequence Player) or a BlendSpace "
			"(creates a Blend Space Player). Any existing player node in the state is replaced.\n"
			"Call compile_anim_blueprint afterwards to verify."))
		.SupportsDryRun()
		.StringArg(TEXT("asset_path"), TEXT("Content path to the UAnimBlueprint"), true)
		.StringArg(TEXT("machine_name"), TEXT("Name of the state machine"), true)
		.StringArg(TEXT("state_name"), TEXT("Name of the state to fill"), true)
		.StringArg(TEXT("animation_path"), TEXT("UAnimSequence or UBlendSpace to play in this state. Must share the AnimBP's skeleton."), true)
		.BoolArg(TEXT("loop"), TEXT("Loop the animation (default: true — the usual choice for locomotion states)"))
		.NumberArg(TEXT("play_rate"), TEXT("Play rate multiplier (default: 1.0)"))
		.Example(TEXT("{\"asset_path\": \"/Game/Characters/ABP_Hero\", \"machine_name\": \"Locomotion\", \"state_name\": \"Idle\", \"animation_path\": \"/Game/Anims/AS_Idle\"}"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			FMCPToolResult Err;
			UAnimBlueprint* AnimBP = LoadAnimBP(Args, Err);
			if (!AnimBP) return Err;

			FString MachineName, StateName, AnimPath;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("machine_name"), MachineName));
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("state_name"), StateName));
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("animation_path"), AnimPath));

			UAnimationStateMachineGraph* SMGraph = FindStateMachineGraph(AnimBP, MachineName, Err);
			if (!SMGraph) return Err;

			UAnimStateNode* State = FindState(SMGraph, StateName, Err);
			if (!State) return Err;

			UAnimationStateGraph* StateGraph = Cast<UAnimationStateGraph>(State->BoundGraph);
			if (!StateGraph)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::Internal,
					FString::Printf(TEXT("State '%s' has no state graph."), *StateName));
			}

			UAnimGraphNode_StateResult* ResultNode = StateGraph->GetResultNode();
			if (!ResultNode)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::Internal,
					FString::Printf(TEXT("State '%s' has no Result node."), *StateName),
					TEXT("The state graph appears malformed; remove and re-add the state."));
			}

			UAnimationAsset* AnimAsset = MCPCommon::LoadAssetChecked<UAnimationAsset>(AnimPath, Err);
			if (!AnimAsset) return Err;
			if (!RequireSameSkeleton(AnimBP->TargetSkeleton, AnimAsset, TEXT("animation_path"), Err)) return Err;

			// Pick the player node class from the asset type.
			UClass* PlayerClass = nullptr;
			if (AnimAsset->IsA<UBlendSpace>())
			{
				PlayerClass = UAnimGraphNode_BlendSpacePlayer::StaticClass();
			}
			else if (AnimAsset->IsA<UAnimSequenceBase>())
			{
				PlayerClass = UAnimGraphNode_SequencePlayer::StaticClass();
			}
			else
			{
				return FMCPToolResult::ErrorStructured(EMCPError::Unsupported,
					FString::Printf(TEXT("'%s' is a %s; states accept an AnimSequence or a BlendSpace."),
						*AnimAsset->GetName(), *AnimAsset->GetClass()->GetName()));
			}

			StateGraph->Modify();

			// Replace any existing player so repeated calls are idempotent rather
			// than leaving orphan nodes behind.
			int32 RemovedPlayers = 0;
			for (int32 i = StateGraph->Nodes.Num() - 1; i >= 0; --i)
			{
				if (UAnimGraphNode_AssetPlayerBase* Existing = Cast<UAnimGraphNode_AssetPlayerBase>(StateGraph->Nodes[i]))
				{
					Existing->Modify();
					Existing->BreakAllNodeLinks();
					StateGraph->RemoveNode(Existing);
					++RemovedPlayers;
				}
			}

			UAnimGraphNode_Base* Player = CreateAnimNode(StateGraph, PlayerClass,
				ResultNode->NodePosX - 350, ResultNode->NodePosY,
				[AnimAsset](UAnimGraphNode_Base* Node)
				{
					if (UAnimGraphNode_AssetPlayerBase* AsPlayer = Cast<UAnimGraphNode_AssetPlayerBase>(Node))
					{
						AsPlayer->SetAnimationAsset(AnimAsset);
					}
				});

			if (!Player)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::Internal,
					FString::Printf(TEXT("Failed to create a %s in state '%s'."), *PlayerClass->GetName(), *StateName));
			}

			// Loop / play rate live on the inner FAnimNode struct.
			bool bLoop = true;
			Args->TryGetBoolField(TEXT("loop"), bLoop);
			const double PlayRate = Args->HasField(TEXT("play_rate")) ? Args->GetNumberField(TEXT("play_rate")) : 1.0;

			for (TFieldIterator<FStructProperty> It(Player->GetClass()); It; ++It)
			{
				FStructProperty* StructProp = *It;
				if (!StructProp->Struct) continue;
				void* StructMemory = StructProp->ContainerPtrToValuePtr<void>(Player);

				if (FBoolProperty* LoopProp = CastField<FBoolProperty>(StructProp->Struct->FindPropertyByName(TEXT("bLoopAnimation"))))
				{
					LoopProp->SetPropertyValue_InContainer(StructMemory, bLoop);
				}
				if (FProperty* RateProp = StructProp->Struct->FindPropertyByName(TEXT("PlayRate")))
				{
					if (FFloatProperty* AsFloat = CastField<FFloatProperty>(RateProp))
					{
						AsFloat->SetPropertyValue_InContainer(StructMemory, (float)PlayRate);
					}
					else if (FDoubleProperty* AsDouble = CastField<FDoubleProperty>(RateProp))
					{
						AsDouble->SetPropertyValue_InContainer(StructMemory, PlayRate);
					}
				}
			}
			Player->ReconstructNode();

			// Wire player -> Result.
			UEdGraphPin* FromPin = FindPosePin(Player, EGPD_Output);
			UEdGraphPin* ToPin = FindPosePin(ResultNode, EGPD_Input);
			if (!FromPin || !ToPin)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::Internal,
					FString::Printf(TEXT("Could not find pose pins to wire the player into state '%s' (%s)."),
						*StateName, *DescribePosePins(Player)));
			}

			ResultNode->Modify();
			if (!ConnectPose(FromPin, ToPin, Err)) return Err;

			MarkAnimBPModified(AnimBP);

			TSharedPtr<FJsonObject> Result = MCPCommon::GraphToJson(StateGraph, /*bIncludePins*/ false);
			Result->SetStringField(TEXT("state"), StateName);
			Result->SetStringField(TEXT("machine"), MachineName);
			Result->SetStringField(TEXT("animation"), AnimAsset->GetPathName());
			Result->SetStringField(TEXT("player_node_class"), PlayerClass->GetName());
			Result->SetStringField(TEXT("player_node_id"), Player->NodeGuid.ToString());
			Result->SetBoolField(TEXT("loop"), bLoop);
			Result->SetNumberField(TEXT("play_rate"), PlayRate);
			Result->SetNumberField(TEXT("replaced_player_count"), RemovedPlayers);

			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("State '%s' in '%s' now plays '%s' via a %s (loop: %s, rate: %.2f)%s. Run compile_anim_blueprint to verify."),
					*StateName, *MachineName, *AnimAsset->GetName(), *PlayerClass->GetName(),
					bLoop ? TEXT("true") : TEXT("false"), PlayRate,
					RemovedPlayers > 0 ? TEXT(", replacing the previous player") : TEXT("")),
				Result);
		});

	// =================================================================
	// set_anim_transition_rule   (fixes D2)
	// =================================================================
	MCP_TOOL(Registry, "set_anim_transition_rule")
		.Description(TEXT(
			"Author the rule that decides when a state machine transition is taken. This is the step "
			"add_anim_transition cannot do on its own: a freshly created transition's rule graph leaves "
			"bCanEnterTransition at false, so the transition never fires no matter what the game does.\n"
			"Rule types:\n"
			"  Always     — always true. Combine with a state whose animation must play out, or with "
			"add_anim_transition's blend time, for unconditional flow.\n"
			"  Never      — always false. Useful to disable a transition without deleting it.\n"
			"  BoolVariable — reads a bool variable on the Animation Blueprint (add it with add_variable and "
			"drive it from NativeUpdateAnimation / BlueprintUpdateAnimation). The everyday case: 'IsMoving', "
			"'IsFalling'. Set invert=true for 'not'.\n"
			"  CurveValue — true while a named animation curve exceeds threshold; lets the animation itself "
			"decide when it may be interrupted.\n"
			"  Automatic  — hands the decision to the engine's automatic rule, which fires as the state's "
			"sequence player approaches its end. The right answer for 'play this out, then move on'."))
		.SupportsDryRun()
		.StringArg(TEXT("asset_path"), TEXT("Content path to the UAnimBlueprint"), true)
		.StringArg(TEXT("machine_name"), TEXT("Name of the state machine"), true)
		.StringArg(TEXT("from_state"), TEXT("Source state of the transition"), true)
		.StringArg(TEXT("to_state"), TEXT("Target state of the transition"), true)
		.EnumArg(TEXT("rule_type"), TEXT("Which rule to author"),
			{ TEXT("Always"), TEXT("Never"), TEXT("BoolVariable"), TEXT("CurveValue"), TEXT("Automatic") }, true)
		.StringArg(TEXT("variable_name"), TEXT("For BoolVariable: the bool variable on the AnimBP to read (e.g. 'IsMoving')"))
		.StringArg(TEXT("curve_name"), TEXT("For CurveValue: the animation curve to read"))
		.NumberArg(TEXT("threshold"), TEXT("For CurveValue: the transition is taken while the curve exceeds this (default: 0.5)"))
		.BoolArg(TEXT("invert"), TEXT("For BoolVariable: take the transition when the variable is FALSE (default: false)"))
		.Example(TEXT("{\"asset_path\": \"/Game/Characters/ABP_Hero\", \"machine_name\": \"Locomotion\", \"from_state\": \"Idle\", \"to_state\": \"Run\", \"rule_type\": \"BoolVariable\", \"variable_name\": \"IsMoving\"}"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			FMCPToolResult Err;
			UAnimBlueprint* AnimBP = LoadAnimBP(Args, Err);
			if (!AnimBP) return Err;

			FString MachineName, FromState, ToState, RuleType;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("machine_name"), MachineName));
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("from_state"), FromState));
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("to_state"), ToState));
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("rule_type"), RuleType));

			const TArray<FString> ValidRules = { TEXT("Always"), TEXT("Never"), TEXT("BoolVariable"), TEXT("CurveValue"), TEXT("Automatic") };
			BAIL_IF_INVALID(FMCPValidate::OneOf(RuleType, ValidRules, TEXT("rule_type")));

			UAnimationStateMachineGraph* SMGraph = FindStateMachineGraph(AnimBP, MachineName, Err);
			if (!SMGraph) return Err;

			UAnimStateTransitionNode* Transition = FindTransition(SMGraph, FromState, ToState, Err);
			if (!Transition) return Err;

			Transition->Modify();

			// The Automatic rule is a node flag, not graph content — the compiler
			// generates the "sequence is nearly done" test itself.
			if (RuleType == TEXT("Automatic"))
			{
				Transition->bAutomaticRuleBasedOnSequencePlayerInState = true;
				MarkAnimBPModified(AnimBP);

				TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
				Result->SetStringField(TEXT("anim_blueprint"), AnimBP->GetPathName());
				Result->SetStringField(TEXT("machine"), MachineName);
				Result->SetStringField(TEXT("from_state"), FromState);
				Result->SetStringField(TEXT("to_state"), ToState);
				Result->SetStringField(TEXT("rule_type"), RuleType);

				return FMCPToolResult::SuccessStructured(
					FString::Printf(TEXT("Transition '%s' -> '%s' now uses the automatic rule: it fires as the state's sequence player nears its end. The source state must contain a sequence player (set_anim_state_animation)."),
						*FromState, *ToState),
					Result);
			}

			Transition->bAutomaticRuleBasedOnSequencePlayerInState = false;

			UAnimationTransitionGraph* RuleGraph = Cast<UAnimationTransitionGraph>(Transition->BoundGraph);
			if (!RuleGraph)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::Internal,
					FString::Printf(TEXT("Transition '%s' -> '%s' has no rule graph."), *FromState, *ToState));
			}

			UAnimGraphNode_TransitionResult* ResultNode = RuleGraph->GetResultNode();
			if (!ResultNode)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::Internal,
					FString::Printf(TEXT("Transition '%s' -> '%s' has no rule Result node."), *FromState, *ToState));
			}

			UEdGraphPin* ConditionPin = ResultNode->FindPin(TEXT("bCanEnterTransition"), EGPD_Input);
			if (!ConditionPin)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::Internal,
					TEXT("The transition Result node has no bCanEnterTransition pin."));
			}

			RuleGraph->Modify();
			ResultNode->Modify();

			// Clear whatever the rule graph held before, so repeated calls replace
			// rather than accumulate orphan nodes.
			ConditionPin->BreakAllPinLinks();
			for (int32 i = RuleGraph->Nodes.Num() - 1; i >= 0; --i)
			{
				UEdGraphNode* Node = RuleGraph->Nodes[i];
				if (Node == ResultNode || !IsValid(Node)) continue;
				Node->Modify();
				Node->BreakAllNodeLinks();
				RuleGraph->RemoveNode(Node);
			}

			FString Summary;

			if (RuleType == TEXT("Always") || RuleType == TEXT("Never"))
			{
				const bool bValue = (RuleType == TEXT("Always"));
				ConditionPin->DefaultValue = bValue ? TEXT("true") : TEXT("false");
				Summary = FString::Printf(TEXT("always %s"), bValue ? TEXT("true") : TEXT("false"));
			}
			else if (RuleType == TEXT("BoolVariable"))
			{
				FString VariableName;
				if (!Args->TryGetStringField(TEXT("variable_name"), VariableName) || VariableName.IsEmpty())
				{
					return FMCPToolResult::ErrorStructured(EMCPError::OutOfRange,
						TEXT("variable_name is required for rule_type 'BoolVariable'."));
				}

				// The variable has to exist on the AnimBP, and be a bool.
				const FName VarFName(*VariableName);
				const int32 VarIndex = FBlueprintEditorUtils::FindNewVariableIndex(AnimBP, VarFName);
				if (VarIndex == INDEX_NONE)
				{
					TArray<FString> Available;
					for (const FBPVariableDescription& Var : AnimBP->NewVariables)
					{
						if (Var.VarType.PinCategory == UEdGraphSchema_K2::PC_Boolean)
						{
							Available.Add(Var.VarName.ToString());
						}
					}
					return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
						FString::Printf(TEXT("AnimBlueprint '%s' has no variable named '%s'."), *AnimBP->GetName(), *VariableName),
						TEXT("Create it with add_variable (type Boolean), then drive it from the AnimBP's update event."),
						Available);
				}
				if (AnimBP->NewVariables[VarIndex].VarType.PinCategory != UEdGraphSchema_K2::PC_Boolean)
				{
					return FMCPToolResult::ErrorStructured(EMCPError::InvalidName,
						FString::Printf(TEXT("Variable '%s' is not a Boolean; a transition rule needs a bool."), *VariableName));
				}

				FGraphNodeCreator<UK2Node_VariableGet> GetterCreator(*RuleGraph);
				UK2Node_VariableGet* Getter = GetterCreator.CreateNode(/*bSelectNewNode*/ false);
				Getter->VariableReference.SetSelfMember(VarFName);
				Getter->NodePosX = ResultNode->NodePosX - 400;
				Getter->NodePosY = ResultNode->NodePosY;
				GetterCreator.Finalize();

				UEdGraphPin* ValuePin = Getter->FindPin(VarFName, EGPD_Output);
				if (!ValuePin)
				{
					return FMCPToolResult::ErrorStructured(EMCPError::Internal,
						FString::Printf(TEXT("The variable getter for '%s' produced no output pin."), *VariableName));
				}

				bool bInvert = false;
				Args->TryGetBoolField(TEXT("invert"), bInvert);

				if (bInvert)
				{
					UFunction* NotFunction = UKismetMathLibrary::StaticClass()->FindFunctionByName(TEXT("Not_PreBool"));
					if (!NotFunction)
					{
						return FMCPToolResult::ErrorStructured(EMCPError::Internal,
							TEXT("UKismetMathLibrary::Not_PreBool was not found."));
					}

					FGraphNodeCreator<UK2Node_CallFunction> NotCreator(*RuleGraph);
					UK2Node_CallFunction* NotNode = NotCreator.CreateNode(/*bSelectNewNode*/ false);
					NotNode->SetFromFunction(NotFunction);
					NotNode->NodePosX = ResultNode->NodePosX - 200;
					NotNode->NodePosY = ResultNode->NodePosY;
					NotCreator.Finalize();

					UEdGraphPin* NotInput = NotNode->FindPin(TEXT("A"), EGPD_Input);
					UEdGraphPin* NotOutput = NotNode->GetReturnValuePin();
					if (!NotInput || !NotOutput)
					{
						return FMCPToolResult::ErrorStructured(EMCPError::Internal,
							TEXT("The Not node is missing its pins."));
					}

					ValuePin->MakeLinkTo(NotInput);
					NotOutput->MakeLinkTo(ConditionPin);
				}
				else
				{
					ValuePin->MakeLinkTo(ConditionPin);
				}

				Summary = FString::Printf(TEXT("%s%s"), bInvert ? TEXT("NOT ") : TEXT(""), *VariableName);
			}
			else // CurveValue
			{
				FString CurveName;
				if (!Args->TryGetStringField(TEXT("curve_name"), CurveName) || CurveName.IsEmpty())
				{
					return FMCPToolResult::ErrorStructured(EMCPError::OutOfRange,
						TEXT("curve_name is required for rule_type 'CurveValue'."));
				}
				const double Threshold = Args->HasField(TEXT("threshold")) ? Args->GetNumberField(TEXT("threshold")) : 0.5;

				UFunction* GetCurve = UAnimInstance::StaticClass()->FindFunctionByName(TEXT("GetCurveValue"));
				UFunction* Greater = UKismetMathLibrary::StaticClass()->FindFunctionByName(TEXT("Greater_DoubleDouble"));
				if (!GetCurve || !Greater)
				{
					return FMCPToolResult::ErrorStructured(EMCPError::Internal,
						TEXT("UAnimInstance::GetCurveValue or UKismetMathLibrary::Greater_DoubleDouble was not found."));
				}

				FGraphNodeCreator<UK2Node_CallFunction> CurveCreator(*RuleGraph);
				UK2Node_CallFunction* CurveNode = CurveCreator.CreateNode(/*bSelectNewNode*/ false);
				CurveNode->SetFromFunction(GetCurve);
				CurveNode->NodePosX = ResultNode->NodePosX - 600;
				CurveNode->NodePosY = ResultNode->NodePosY;
				CurveCreator.Finalize();

				if (UEdGraphPin* CurveNamePin = CurveNode->FindPin(TEXT("CurveName"), EGPD_Input))
				{
					CurveNamePin->DefaultValue = CurveName;
				}

				FGraphNodeCreator<UK2Node_CallFunction> CompareCreator(*RuleGraph);
				UK2Node_CallFunction* CompareNode = CompareCreator.CreateNode(/*bSelectNewNode*/ false);
				CompareNode->SetFromFunction(Greater);
				CompareNode->NodePosX = ResultNode->NodePosX - 250;
				CompareNode->NodePosY = ResultNode->NodePosY;
				CompareCreator.Finalize();

				UEdGraphPin* CurveOut = CurveNode->GetReturnValuePin();
				UEdGraphPin* PinA = CompareNode->FindPin(TEXT("A"), EGPD_Input);
				UEdGraphPin* PinB = CompareNode->FindPin(TEXT("B"), EGPD_Input);
				UEdGraphPin* CompareOut = CompareNode->GetReturnValuePin();

				if (!CurveOut || !PinA || !PinB || !CompareOut)
				{
					return FMCPToolResult::ErrorStructured(EMCPError::Internal,
						TEXT("The curve-comparison nodes are missing expected pins."));
				}

				CurveOut->MakeLinkTo(PinA);
				PinB->DefaultValue = FString::SanitizeFloat(Threshold);
				CompareOut->MakeLinkTo(ConditionPin);

				Summary = FString::Printf(TEXT("curve '%s' > %.3f"), *CurveName, Threshold);
			}

			MarkAnimBPModified(AnimBP);

			TSharedPtr<FJsonObject> Result = MCPCommon::GraphToJson(RuleGraph, /*bIncludePins*/ false);
			Result->SetStringField(TEXT("anim_blueprint"), AnimBP->GetPathName());
			Result->SetStringField(TEXT("machine"), MachineName);
			Result->SetStringField(TEXT("from_state"), FromState);
			Result->SetStringField(TEXT("to_state"), ToState);
			Result->SetStringField(TEXT("rule_type"), RuleType);
			Result->SetStringField(TEXT("rule"), Summary);

			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Transition '%s' -> '%s' now fires when: %s. Run compile_anim_blueprint to verify."),
					*FromState, *ToState, *Summary),
				Result);
		});

	// =================================================================
	// set_anim_transition_settings
	// =================================================================
	MCP_TOOL(Registry, "set_anim_transition_settings")
		.Description(TEXT(
			"Set a transition's blend properties: crossfade duration, interpolation curve, per-bone blend "
			"profile, and priority order (when two transitions out of one state are both true in the same "
			"frame, the lower priority number wins). Duration is the single biggest lever on how a state "
			"machine feels — 0.1s reads as a snap, 0.3s as a settle."))
		.Idempotent()
		.SupportsDryRun()
		.StringArg(TEXT("asset_path"), TEXT("Content path to the UAnimBlueprint"), true)
		.StringArg(TEXT("machine_name"), TEXT("Name of the state machine"), true)
		.StringArg(TEXT("from_state"), TEXT("Source state of the transition"), true)
		.StringArg(TEXT("to_state"), TEXT("Target state of the transition"), true)
		.NumberArg(TEXT("duration"), TEXT("Crossfade duration in seconds (e.g. 0.2)"))
		.EnumArg(TEXT("blend_mode"), TEXT("Interpolation curve for the crossfade"),
			{ TEXT("Linear"), TEXT("Cubic"), TEXT("HermiteCubic"), TEXT("Sinusoidal"), TEXT("QuadraticInOut"),
			  TEXT("CubicInOut"), TEXT("QuarticInOut"), TEXT("QuinticInOut"), TEXT("CircularIn"),
			  TEXT("CircularOut"), TEXT("CircularInOut"), TEXT("ExpIn"), TEXT("ExpOut"), TEXT("ExpInOut"), TEXT("Custom") })
		.StringArg(TEXT("blend_profile"), TEXT("Per-bone blend profile name from the skeleton, or empty to clear"))
		.IntArg(TEXT("priority_order"), TEXT("Evaluation priority; lower wins when several transitions are simultaneously true"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			FMCPToolResult Err;
			UAnimBlueprint* AnimBP = LoadAnimBP(Args, Err);
			if (!AnimBP) return Err;

			FString MachineName, FromState, ToState;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("machine_name"), MachineName));
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("from_state"), FromState));
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("to_state"), ToState));

			UAnimationStateMachineGraph* SMGraph = FindStateMachineGraph(AnimBP, MachineName, Err);
			if (!SMGraph) return Err;

			UAnimStateTransitionNode* Transition = FindTransition(SMGraph, FromState, ToState, Err);
			if (!Transition) return Err;

			bool bChanged = false;
			Transition->Modify();

			if (Args->HasField(TEXT("duration")))
			{
				const double Duration = Args->GetNumberField(TEXT("duration"));
				BAIL_IF_INVALID(FMCPValidate::InRangeF(Duration, 0.0, 60.0, TEXT("duration")));
				Transition->CrossfadeDuration = (float)Duration;
				bChanged = true;
			}

			FString BlendModeName;
			if (Args->TryGetStringField(TEXT("blend_mode"), BlendModeName) && !BlendModeName.IsEmpty())
			{
				const UEnum* Enum = StaticEnum<EAlphaBlendOption>();
				const int64 Value = Enum->GetValueByNameString(BlendModeName);
				if (Value == INDEX_NONE)
				{
					TArray<FString> Valid;
					for (int32 i = 0; i < Enum->NumEnums() - 1; ++i) { Valid.Add(Enum->GetNameStringByIndex(i)); }
					return FMCPToolResult::ErrorStructured(EMCPError::OutOfRange,
						FString::Printf(TEXT("Unknown blend_mode '%s'."), *BlendModeName), FString(), Valid);
				}
				Transition->BlendMode = static_cast<EAlphaBlendOption>(Value);
				bChanged = true;
			}

			if (Args->HasField(TEXT("priority_order")))
			{
				Transition->PriorityOrder = (int32)Args->GetNumberField(TEXT("priority_order"));
				bChanged = true;
			}

			FString ProfileName;
			if (Args->HasField(TEXT("blend_profile")))
			{
				Args->TryGetStringField(TEXT("blend_profile"), ProfileName);
				// FBlendProfileInterfaceWrapper keeps its BlendProfile member
				// private; SetSkeletonBlendProfile is the supported way in.
				if (ProfileName.IsEmpty())
				{
					Transition->BlendProfileWrapper.SetSkeletonBlendProfile(nullptr);
				}
				else
				{
					UBlendProfile* Profile = ResolveBlendProfile(AnimBP->TargetSkeleton, ProfileName, Err);
					if (!Profile) return Err;
					Transition->BlendProfileWrapper.SetSkeletonBlendProfile(Profile);
				}
				bChanged = true;
			}

			if (!bChanged)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::OutOfRange,
					TEXT("No settings were supplied."),
					TEXT("Pass at least one of duration, blend_mode, blend_profile, priority_order."));
			}

			MarkAnimBPModified(AnimBP);

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("anim_blueprint"), AnimBP->GetPathName());
			Result->SetStringField(TEXT("machine"), MachineName);
			Result->SetStringField(TEXT("from_state"), FromState);
			Result->SetStringField(TEXT("to_state"), ToState);
			Result->SetNumberField(TEXT("duration"), Transition->CrossfadeDuration);
			Result->SetStringField(TEXT("blend_mode"),
				StaticEnum<EAlphaBlendOption>()->GetNameStringByValue((int64)Transition->BlendMode));
			Result->SetNumberField(TEXT("priority_order"), Transition->PriorityOrder);
			const TObjectPtr<UBlendProfile> ActiveProfile = Transition->BlendProfileWrapper.GetBlendProfile();
			Result->SetStringField(TEXT("blend_profile"), ActiveProfile ? ActiveProfile->GetName() : TEXT(""));

			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Transition '%s' -> '%s': duration %.3fs, blend %s, priority %d."),
					*FromState, *ToState, Transition->CrossfadeDuration,
					*StaticEnum<EAlphaBlendOption>()->GetNameStringByValue((int64)Transition->BlendMode),
					Transition->PriorityOrder),
				Result);
		});

	// =================================================================
	// set_anim_state_machine_entry
	// =================================================================
	MCP_TOOL(Registry, "set_anim_state_machine_entry")
		.Description(TEXT(
			"Set which state a state machine starts in, by wiring its Entry node to that state. A state "
			"machine whose entry is unconnected has no starting state and fails to compile — this is the "
			"step that is easy to forget after building states with add_anim_state."))
		.Idempotent()
		.SupportsDryRun()
		.StringArg(TEXT("asset_path"), TEXT("Content path to the UAnimBlueprint"), true)
		.StringArg(TEXT("machine_name"), TEXT("Name of the state machine"), true)
		.StringArg(TEXT("state_name"), TEXT("State the machine should start in"), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			FMCPToolResult Err;
			UAnimBlueprint* AnimBP = LoadAnimBP(Args, Err);
			if (!AnimBP) return Err;

			FString MachineName, StateName;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("machine_name"), MachineName));
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("state_name"), StateName));

			UAnimationStateMachineGraph* SMGraph = FindStateMachineGraph(AnimBP, MachineName, Err);
			if (!SMGraph) return Err;

			UAnimStateNode* State = FindState(SMGraph, StateName, Err);
			if (!State) return Err;

			UAnimStateEntryNode* EntryNode = SMGraph->EntryNode;
			if (!EntryNode)
			{
				EntryNode = FindNodeOfType<UAnimStateEntryNode>(SMGraph);
			}
			if (!EntryNode)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
					FString::Printf(TEXT("State machine '%s' has no Entry node."), *MachineName),
					TEXT("The state machine graph appears malformed; recreate it with create_anim_state_machine."));
			}

			UEdGraphPin* EntryPin = nullptr;
			for (UEdGraphPin* Pin : EntryNode->Pins)
			{
				if (Pin && Pin->Direction == EGPD_Output) { EntryPin = Pin; break; }
			}
			UEdGraphPin* StatePin = nullptr;
			for (UEdGraphPin* Pin : State->Pins)
			{
				if (Pin && Pin->Direction == EGPD_Input) { StatePin = Pin; break; }
			}

			if (!EntryPin || !StatePin)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::Internal,
					TEXT("Entry or state node is missing the pin needed to connect them."));
			}

			FString Previous;
			if (EntryPin->LinkedTo.Num() > 0 && EntryPin->LinkedTo[0])
			{
				if (UAnimStateNodeBase* Old = Cast<UAnimStateNodeBase>(EntryPin->LinkedTo[0]->GetOwningNode()))
				{
					Previous = Old->GetStateName();
				}
			}

			SMGraph->Modify();
			EntryNode->Modify();
			State->Modify();

			EntryPin->BreakAllPinLinks();
			EntryPin->MakeLinkTo(StatePin);

			MarkAnimBPModified(AnimBP);

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("anim_blueprint"), AnimBP->GetPathName());
			Result->SetStringField(TEXT("machine"), MachineName);
			Result->SetStringField(TEXT("entry_state"), StateName);
			if (!Previous.IsEmpty())
			{
				Result->SetStringField(TEXT("previous_entry_state"), Previous);
			}

			return FMCPToolResult::SuccessStructured(
				Previous.IsEmpty()
					? FString::Printf(TEXT("State machine '%s' now starts in '%s'."), *MachineName, *StateName)
					: FString::Printf(TEXT("State machine '%s' now starts in '%s' (was '%s')."), *MachineName, *StateName, *Previous),
				Result);
		});

	// =================================================================
	// remove_anim_state
	// =================================================================
	MCP_TOOL(Registry, "remove_anim_state")
		.Description(TEXT(
			"Remove a state from a state machine, together with every transition into or out of it. The "
			"result reports which transitions went with it. If the removed state was the machine's entry "
			"state, the machine is left with no starting state and will not compile until "
			"set_anim_state_machine_entry is called."))
		.Destructive()
		.SupportsDryRun()
		.StringArg(TEXT("asset_path"), TEXT("Content path to the UAnimBlueprint"), true)
		.StringArg(TEXT("machine_name"), TEXT("Name of the state machine"), true)
		.StringArg(TEXT("state_name"), TEXT("State to remove"), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			FMCPToolResult Err;
			UAnimBlueprint* AnimBP = LoadAnimBP(Args, Err);
			if (!AnimBP) return Err;

			FString MachineName, StateName;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("machine_name"), MachineName));
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("state_name"), StateName));

			UAnimationStateMachineGraph* SMGraph = FindStateMachineGraph(AnimBP, MachineName, Err);
			if (!SMGraph) return Err;

			UAnimStateNode* State = FindState(SMGraph, StateName, Err);
			if (!State) return Err;

			// Was this the entry state?
			bool bWasEntry = false;
			if (UAnimStateEntryNode* EntryNode = SMGraph->EntryNode)
			{
				for (UEdGraphPin* Pin : EntryNode->Pins)
				{
					if (!Pin || Pin->Direction != EGPD_Output) continue;
					for (UEdGraphPin* Linked : Pin->LinkedTo)
					{
						if (Linked && Linked->GetOwningNode() == State) { bWasEntry = true; }
					}
				}
			}

			SMGraph->Modify();

			// Collect and remove attached transitions first.
			TArray<FString> RemovedTransitions;
			for (int32 i = SMGraph->Nodes.Num() - 1; i >= 0; --i)
			{
				UAnimStateTransitionNode* Transition = Cast<UAnimStateTransitionNode>(SMGraph->Nodes[i]);
				if (!Transition) continue;

				UAnimStateNodeBase* Prev = Transition->GetPreviousState();
				UAnimStateNodeBase* Next = Transition->GetNextState();
				if (Prev != State && Next != State) continue;

				RemovedTransitions.Add(FString::Printf(TEXT("%s -> %s"),
					Prev ? *Prev->GetStateName() : TEXT("(none)"),
					Next ? *Next->GetStateName() : TEXT("(none)")));

				Transition->Modify();
				Transition->BreakAllNodeLinks();
				SMGraph->RemoveNode(Transition);
			}

			State->Modify();
			State->BreakAllNodeLinks();
			SMGraph->RemoveNode(State);

			MarkAnimBPModified(AnimBP);

			TArray<TSharedPtr<FJsonValue>> TransitionJson;
			for (const FString& Name : RemovedTransitions)
			{
				TransitionJson.Add(MakeShared<FJsonValueString>(Name));
			}

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("anim_blueprint"), AnimBP->GetPathName());
			Result->SetStringField(TEXT("machine"), MachineName);
			Result->SetStringField(TEXT("removed_state"), StateName);
			Result->SetArrayField(TEXT("removed_transitions"), TransitionJson);
			Result->SetBoolField(TEXT("was_entry_state"), bWasEntry);

			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Removed state '%s' from '%s' along with %d transition(s).%s"),
					*StateName, *MachineName, RemovedTransitions.Num(),
					bWasEntry
						? TEXT(" It was the ENTRY state — the machine will not compile until you call set_anim_state_machine_entry.")
						: TEXT("")),
				Result);
		});

	// =================================================================
	// remove_anim_transition
	// =================================================================
	MCP_TOOL(Registry, "remove_anim_transition")
		.Description(TEXT(
			"Remove a transition between two states, leaving both states in place. To disable a transition "
			"without losing its rule, use set_anim_transition_rule with rule_type 'Never' instead."))
		.Destructive()
		.SupportsDryRun()
		.StringArg(TEXT("asset_path"), TEXT("Content path to the UAnimBlueprint"), true)
		.StringArg(TEXT("machine_name"), TEXT("Name of the state machine"), true)
		.StringArg(TEXT("from_state"), TEXT("Source state of the transition"), true)
		.StringArg(TEXT("to_state"), TEXT("Target state of the transition"), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			FMCPToolResult Err;
			UAnimBlueprint* AnimBP = LoadAnimBP(Args, Err);
			if (!AnimBP) return Err;

			FString MachineName, FromState, ToState;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("machine_name"), MachineName));
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("from_state"), FromState));
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("to_state"), ToState));

			UAnimationStateMachineGraph* SMGraph = FindStateMachineGraph(AnimBP, MachineName, Err);
			if (!SMGraph) return Err;

			UAnimStateTransitionNode* Transition = FindTransition(SMGraph, FromState, ToState, Err);
			if (!Transition) return Err;

			SMGraph->Modify();
			Transition->Modify();
			Transition->BreakAllNodeLinks();
			SMGraph->RemoveNode(Transition);

			MarkAnimBPModified(AnimBP);

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("anim_blueprint"), AnimBP->GetPathName());
			Result->SetStringField(TEXT("machine"), MachineName);
			Result->SetStringField(TEXT("removed_transition"), FString::Printf(TEXT("%s -> %s"), *FromState, *ToState));

			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Removed transition '%s' -> '%s' from machine '%s'."),
					*FromState, *ToState, *MachineName),
				Result);
		});

	// =================================================================
	// compile_anim_blueprint   (fixes D11)
	// =================================================================
	MCP_TOOL(Registry, "compile_anim_blueprint")
		.Description(TEXT(
			"Compile an Animation Blueprint and report the result: error and warning counts plus the "
			"compiler messages. None of the AnimGraph editing tools compile on their own — they mark the "
			"Blueprint modified and leave compilation to you, so a batch of edits costs one compile instead "
			"of ten. Call this at the end of any authoring sequence: it is the only way to know that a state "
			"machine is actually valid (unconnected entry, empty states and dangling pose links all show up "
			"here). Pass save=true to write the asset to disk when it compiles clean."))
		.SupportsDryRun()
		.LongRunning()
		.StringArg(TEXT("asset_path"), TEXT("Content path to the UAnimBlueprint"), true)
		.BoolArg(TEXT("save"), TEXT("Save the asset when compilation produces no errors (default: false)"))
		.Example(TEXT("{\"asset_path\": \"/Game/Characters/ABP_Hero\", \"save\": true}"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			FMCPToolResult Err;
			UAnimBlueprint* AnimBP = LoadAnimBP(Args, Err);
			if (!AnimBP) return Err;

			bool bSave = false;
			Args->TryGetBoolField(TEXT("save"), bSave);

			TSharedPtr<FJsonObject> Result;
			const bool bOk = CompileAnimBlueprint(AnimBP, bSave, Result);

			int32 ErrorCount = 0, WarningCount = 0;
			Result->TryGetNumberField(TEXT("error_count"), ErrorCount);
			Result->TryGetNumberField(TEXT("warning_count"), WarningCount);

			if (!bOk)
			{
				// A failed compile is a real outcome the agent must react to, not
				// a tool malfunction — return it as an error with the messages
				// attached so the agent can fix the graph.
				FMCPToolResult Failure = FMCPToolResult::ErrorStructured(EMCPError::Internal,
					FString::Printf(TEXT("'%s' failed to compile: %d error(s), %d warning(s)."),
						*AnimBP->GetName(), ErrorCount, WarningCount),
					TEXT("Use animgraph_describe to inspect the graph. Common causes: the AnimGraph's Output Pose is unconnected, a state has no animation (set_anim_state_animation), or the state machine has no entry state (set_anim_state_machine_entry)."));
				Failure.StructuredContent = Result;
				return Failure;
			}

			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("'%s' compiled cleanly%s%s."),
					*AnimBP->GetName(),
					WarningCount > 0 ? *FString::Printf(TEXT(" with %d warning(s)"), WarningCount) : TEXT(""),
					bSave ? TEXT(" and was saved") : TEXT("")),
				Result);
		});
}

} // namespace MCPAnimTools::AnimGraphNodes
