// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "MCPToolRegistry.h"
#include "MCPProtocol.h"
#include "MCPToolBuilder.h"

#include "Editor.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/BlendSpace.h"
#include "Animation/BlendSpace1D.h"
#include "Animation/AimOffsetBlendSpace.h"
#include "Animation/AimOffsetBlendSpace1D.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimCompositeBase.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Animation/Skeleton.h"
#include "AnimationStateMachineGraph.h"
#include "Factories/AnimBlueprintFactory.h"
#include "Factories/BlendSpaceFactoryNew.h"
#include "Factories/AimOffsetBlendSpaceFactoryNew.h"
#include "Factories/AnimMontageFactory.h"
#include "AnimationStateMachineSchema.h"
#include "AnimGraphNode_StateMachine.h"
#include "AnimStateNode.h"
#include "AnimStateTransitionNode.h"
#include "AnimationGraph.h"
#include "AnimGraphNode_Root.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "Tools/AnimGraph/AnimGraphCommon.h"
#include "EdGraph/EdGraph.h"   // FGraphNodeCreator

namespace MCPAnimGraphTools::StateMachine
{

using namespace MCPAnimGraphTools::Common;

void RegisterAll(FMCPToolRegistry& Registry)
{
	MCP_TOOL(Registry, "create_anim_state_machine")
		.Description(TEXT(
			"Add a state machine node to an Animation Blueprint's AnimGraph. State machines manage animation "
			"states and the transitions between them (Idle -> Walk -> Run -> Jump).\n"
			"The node is created UNCONNECTED and empty. A working machine needs all of: add_anim_state per "
			"state, set_anim_state_animation to give each state a pose, add_anim_transition + "
			"set_anim_transition_rule per edge, set_anim_state_machine_entry to pick the start state, and "
			"animgraph_connect_pose to wire the machine into the Output Pose. Finish with "
			"compile_anim_blueprint."))
		.StringArg(TEXT("asset_path"), TEXT("Content path to the AnimBlueprint"), true)
		.StringArg(TEXT("machine_name"), TEXT("Name for the state machine (e.g., 'Locomotion')"), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));

			FString MachineName;
			if (!Args->TryGetStringField(TEXT("machine_name"), MachineName))
				return FMCPToolResult::Error(TEXT("machine_name is required"));

			UAnimBlueprint* AnimBP = LoadObject<UAnimBlueprint>(nullptr, *AssetPath);
			if (!IsValid(AnimBP))
				return FMCPToolResult::Error(FString::Printf(TEXT("AnimBlueprint not found: %s"), *AssetPath));

			// Find the AnimGraph
			UEdGraph* AnimGraph = nullptr;
			for (UEdGraph* Graph : AnimBP->FunctionGraphs)
			{
				if (Graph && Graph->GetFName() == TEXT("AnimGraph"))
				{
					AnimGraph = Graph;
					break;
				}
			}

			if (!AnimGraph)
				return FMCPToolResult::Error(TEXT("AnimGraph not found in the AnimBlueprint"));

			AnimGraph->Modify();

			// v4.6: FGraphNodeCreator instead of hand-rolled NewObject + AddNode +
			// PostPlacedNewNode + AllocateDefaultPins. UAnimGraphNode_StateMachineBase
			// allocates its EditorStateMachineGraph inside PostPlacedNewNode behind a
			// check(EditorStateMachineGraph == NULL), so the call order matters;
			// FGraphNodeCreator is the order the engine's own schema uses.
			UAnimGraphNode_StateMachine* SMNode = nullptr;
			{
				FGraphNodeCreator<UAnimGraphNode_StateMachine> NodeCreator(*AnimGraph);
				SMNode = NodeCreator.CreateNode(/*bSelectNewNode*/ false);
				SMNode->NodePosX = 200;
				SMNode->NodePosY = 0;
				NodeCreator.Finalize();
			}

			if (!SMNode)
				return FMCPToolResult::ErrorStructured(EMCPError::Internal, TEXT("Failed to create the state machine node."));

			if (SMNode->EditorStateMachineGraph)
			{
				SMNode->OnRenameNode(MachineName);
			}

			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
			AnimBP->MarkPackageDirty();

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("anim_blueprint"), AnimBP->GetPathName());
			Result->SetStringField(TEXT("machine"), MachineName);
			Result->SetStringField(TEXT("node_id"), SMNode->NodeGuid.ToString());
			Result->SetStringField(TEXT("graph_name"),
				SMNode->EditorStateMachineGraph ? SMNode->EditorStateMachineGraph->GetName() : MachineName);

			return FMCPToolResult::SuccessStructured(
				FString::Printf(
					TEXT("Created state machine '%s' in AnimBP '%s' (node id %s). Next: add_anim_state for each state, "
					     "set_anim_state_animation to give each one a pose, add_anim_transition + set_anim_transition_rule "
					     "to connect them, set_anim_state_machine_entry to pick the start state, and "
					     "animgraph_connect_pose to wire this machine into the Output Pose."),
					*MachineName, *AnimBP->GetName(), *SMNode->NodeGuid.ToString()),
				Result);
		});
	MCP_TOOL(Registry, "add_anim_state")
		.Description(TEXT(
			"Add a state to an existing state machine in an Animation Blueprint. Each state represents an "
			"animation state (Idle, Walk, Run, Jump).\n"
			"A new state is EMPTY: its graph contains only a Result node, so until set_anim_state_animation "
			"gives it a pose the character T-poses whenever that state is active. The usual sequence is "
			"add_anim_state -> set_anim_state_animation -> add_anim_transition -> set_anim_transition_rule."))
		.StringArg(TEXT("asset_path"), TEXT("Content path to the AnimBlueprint"), true)
		.StringArg(TEXT("machine_name"), TEXT("Name of the state machine (as shown in get_anim_state_machine_info)"), true)
		.StringArg(TEXT("state_name"), TEXT("Name for the new state (e.g., 'Idle', 'Walk', 'Jump')"), true)
		.NumberArg(TEXT("position_x"), TEXT("Node X position in the graph (default: 300)"))
		.NumberArg(TEXT("position_y"), TEXT("Node Y position in the graph (default: 0)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath, MachineName, StateName;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));
			if (!Args->TryGetStringField(TEXT("machine_name"), MachineName))
				return FMCPToolResult::Error(TEXT("machine_name is required"));
			if (!Args->TryGetStringField(TEXT("state_name"), StateName))
				return FMCPToolResult::Error(TEXT("state_name is required"));

			double PosX = 300.0, PosY = 0.0;
			if (Args->HasField(TEXT("position_x"))) PosX = Args->GetNumberField(TEXT("position_x"));
			if (Args->HasField(TEXT("position_y"))) PosY = Args->GetNumberField(TEXT("position_y"));

			UAnimBlueprint* AnimBP = LoadObject<UAnimBlueprint>(nullptr, *AssetPath);
			if (!IsValid(AnimBP))
				return FMCPToolResult::Error(FString::Printf(TEXT("AnimBlueprint not found: %s"), *AssetPath));

			// Find the state machine
			UAnimationStateMachineGraph* SMGraph = nullptr;
			FString FoundMachineName;

			for (UEdGraph* Graph : AnimBP->FunctionGraphs)
			{
				if (!Graph) continue;
				for (UEdGraphNode* Node : Graph->Nodes)
				{
					UAnimGraphNode_StateMachine* SMNode = Cast<UAnimGraphNode_StateMachine>(Node);
					if (!SMNode || !SMNode->EditorStateMachineGraph) continue;

					// v4.6: exact match. This used to be NodeTitle.Contains(MachineName),
					// which resolves 'Loco' to a machine named 'Locomotion' — and the
					// v4.6 tools (set_anim_state_animation, set_anim_transition_rule)
					// match exactly, so a substring hit here would silently edit a
					// different machine than the follow-up calls.
					const FString GraphName = SMNode->EditorStateMachineGraph->GetName();
					if (GraphName.Equals(MachineName, ESearchCase::IgnoreCase) ||
						SMNode->GetStateMachineName().Equals(MachineName, ESearchCase::IgnoreCase))
					{
						SMGraph = SMNode->EditorStateMachineGraph;
						FoundMachineName = GraphName;
						break;
					}
				}
				if (SMGraph) break;
			}

			if (!SMGraph)
				return FMCPToolResult::Error(FString::Printf(
					TEXT("State machine '%s' not found. Use get_anim_state_machine_info to list available machines."), *MachineName));

			// Check for duplicate state name
			for (UEdGraphNode* ExistingNode : SMGraph->Nodes)
			{
				if (UAnimStateNode* ExistingState = Cast<UAnimStateNode>(ExistingNode))
				{
					if (ExistingState->GetStateName() == StateName)
						return FMCPToolResult::Error(FString::Printf(TEXT("State '%s' already exists in machine '%s'"), *StateName, *MachineName));
				}
			}

			SMGraph->Modify();

			// v4.6: FGraphNodeCreator. UAnimStateNode::PostPlacedNewNode creates the
			// state's BoundGraph behind a check(BoundGraph == NULL), so the creation
			// order must match the engine's.
			UAnimStateNode* StateNode = nullptr;
			{
				FGraphNodeCreator<UAnimStateNode> NodeCreator(*SMGraph);
				StateNode = NodeCreator.CreateNode(/*bSelectNewNode*/ false);
				StateNode->NodePosX = (int32)PosX;
				StateNode->NodePosY = (int32)PosY;
				NodeCreator.Finalize();
			}

			if (!StateNode)
				return FMCPToolResult::ErrorStructured(EMCPError::Internal, TEXT("Failed to create the state node."));

			StateNode->OnRenameNode(StateName);

			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
			AnimBP->MarkPackageDirty();

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("anim_blueprint"), AnimBP->GetPathName());
			Result->SetStringField(TEXT("machine"), MachineName);
			Result->SetStringField(TEXT("state"), StateName);
			Result->SetStringField(TEXT("node_id"), StateNode->NodeGuid.ToString());
			Result->SetBoolField(TEXT("has_animation"), false);

			return FMCPToolResult::SuccessStructured(
				FString::Printf(
					TEXT("Added state '%s' to state machine '%s' (node id %s). The state is EMPTY and will evaluate "
					     "to reference pose (a T-pose) until you call set_anim_state_animation on it. Then use "
					     "add_anim_transition to connect it."),
					*StateName, *MachineName, *StateNode->NodeGuid.ToString()),
				Result);
		});
	MCP_TOOL(Registry, "add_anim_transition")
		.Description(TEXT(
			"Add a transition between two states in an Animation Blueprint state machine, with a crossfade "
			"duration. A new transition's RULE is empty, which means bCanEnterTransition stays false and the "
			"transition can never actually be taken — call set_anim_transition_rule afterwards to say when it "
			"fires, then compile_anim_blueprint to verify. Use get_anim_state_machine_info to see available "
			"states, and set_anim_transition_settings for blend mode, blend profile and priority."))
		.StringArg(TEXT("asset_path"), TEXT("Content path to the AnimBlueprint"), true)
		.StringArg(TEXT("machine_name"), TEXT("Name of the state machine"), true)
		.StringArg(TEXT("from_state"), TEXT("Source state name"), true)
		.StringArg(TEXT("to_state"), TEXT("Target state name"), true)
		.NumberArg(TEXT("transition_duration"), TEXT("Blend duration in seconds (default: 0.2)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath, MachineName, FromState, ToState;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));
			if (!Args->TryGetStringField(TEXT("machine_name"), MachineName))
				return FMCPToolResult::Error(TEXT("machine_name is required"));
			if (!Args->TryGetStringField(TEXT("from_state"), FromState))
				return FMCPToolResult::Error(TEXT("from_state is required"));
			if (!Args->TryGetStringField(TEXT("to_state"), ToState))
				return FMCPToolResult::Error(TEXT("to_state is required"));

			double TransDuration = 0.2;
			if (Args->HasField(TEXT("transition_duration")))
				TransDuration = Args->GetNumberField(TEXT("transition_duration"));

			UAnimBlueprint* AnimBP = LoadObject<UAnimBlueprint>(nullptr, *AssetPath);
			if (!IsValid(AnimBP))
				return FMCPToolResult::Error(FString::Printf(TEXT("AnimBlueprint not found: %s"), *AssetPath));

			// Find the state machine graph
			UAnimationStateMachineGraph* SMGraph = nullptr;
			for (UEdGraph* Graph : AnimBP->FunctionGraphs)
			{
				if (!Graph) continue;
				for (UEdGraphNode* Node : Graph->Nodes)
				{
					UAnimGraphNode_StateMachine* SMNode = Cast<UAnimGraphNode_StateMachine>(Node);
					if (!SMNode || !SMNode->EditorStateMachineGraph) continue;

					// v4.6: exact match — see the note in add_anim_state.
					const FString GraphName = SMNode->EditorStateMachineGraph->GetName();
					if (GraphName.Equals(MachineName, ESearchCase::IgnoreCase) ||
						SMNode->GetStateMachineName().Equals(MachineName, ESearchCase::IgnoreCase))
					{
						SMGraph = SMNode->EditorStateMachineGraph;
						break;
					}
				}
				if (SMGraph) break;
			}

			if (!SMGraph)
				return FMCPToolResult::Error(FString::Printf(TEXT("State machine '%s' not found"), *MachineName));

			// Find from and to state nodes
			UAnimStateNode* FromNode = nullptr;
			UAnimStateNode* ToNode = nullptr;

			for (UEdGraphNode* Node : SMGraph->Nodes)
			{
				UAnimStateNode* StateNode = Cast<UAnimStateNode>(Node);
				if (!StateNode) continue;

				if (StateNode->GetStateName() == FromState) FromNode = StateNode;
				if (StateNode->GetStateName() == ToState) ToNode = StateNode;
			}

			if (!FromNode)
				return FMCPToolResult::Error(FString::Printf(TEXT("Source state '%s' not found in machine '%s'"), *FromState, *MachineName));
			if (!ToNode)
				return FMCPToolResult::Error(FString::Printf(TEXT("Target state '%s' not found in machine '%s'"), *ToState, *MachineName));

			// Refuse a duplicate: a second transition between the same two states
			// is legal in the editor but almost always a mistake from a retry, and
			// it makes set_anim_transition_rule ambiguous about which one it edits.
			for (UEdGraphNode* Node : SMGraph->Nodes)
			{
				UAnimStateTransitionNode* Existing = Cast<UAnimStateTransitionNode>(Node);
				if (!Existing) continue;

				UAnimStateNodeBase* Prev = Existing->GetPreviousState();
				UAnimStateNodeBase* Next = Existing->GetNextState();
				if (Prev == FromNode && Next == ToNode)
				{
					return FMCPToolResult::ErrorStructured(EMCPError::AlreadyExists,
						FString::Printf(TEXT("A transition '%s' -> '%s' already exists in machine '%s'."),
							*FromState, *ToState, *MachineName),
						TEXT("Adjust it with set_anim_transition_settings / set_anim_transition_rule, or remove it with remove_anim_transition."));
				}
			}

			SMGraph->Modify();

			// v4.6: engine-canonical construction. The v4.5 version hand-rolled
			// NewObject + AddNode + PostPlacedNewNode + AllocateDefaultPins and
			// then wired pins by scanning for the first input/output it found.
			// FGraphNodeCreator runs the same order the state machine schema uses,
			// and UAnimStateTransitionNode::CreateConnections is the engine's own
			// wiring routine — it also handles the bidirectional and shared-rule
			// bookkeeping the manual pin scan silently skipped.
			UAnimStateTransitionNode* TransNode = nullptr;
			{
				FGraphNodeCreator<UAnimStateTransitionNode> NodeCreator(*SMGraph);
				TransNode = NodeCreator.CreateNode(/*bSelectNewNode*/ false);
				TransNode->NodePosX = (FromNode->NodePosX + ToNode->NodePosX) / 2;
				TransNode->NodePosY = (FromNode->NodePosY + ToNode->NodePosY) / 2;
				NodeCreator.Finalize();
			}

			if (!TransNode)
				return FMCPToolResult::ErrorStructured(EMCPError::Internal, TEXT("Failed to create the transition node."));

			TransNode->CreateConnections(FromNode, ToNode);
			TransNode->CrossfadeDuration = (float)TransDuration;

			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
			AnimBP->MarkPackageDirty();

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("anim_blueprint"), AnimBP->GetPathName());
			Result->SetStringField(TEXT("machine"), MachineName);
			Result->SetStringField(TEXT("from_state"), FromState);
			Result->SetStringField(TEXT("to_state"), ToState);
			Result->SetNumberField(TEXT("duration"), TransDuration);
			Result->SetStringField(TEXT("node_id"), TransNode->NodeGuid.ToString());
			Result->SetBoolField(TEXT("rule_authored"), false);

			return FMCPToolResult::SuccessStructured(
				FString::Printf(
					TEXT("Added transition '%s' -> '%s' in machine '%s' (duration %.2fs, node id %s). "
					     "Its rule is EMPTY, so it will never fire — call set_anim_transition_rule next."),
					*FromState, *ToState, *MachineName, TransDuration, *TransNode->NodeGuid.ToString()),
				Result);
		});
	MCP_TOOL(Registry, "get_anim_state_machine_info")
		.Description(TEXT("Get detailed information about all state machines in an Animation Blueprint: state names, transitions, default state, and connected animations. Use this to understand the structure before modifying."))
		.ReadOnly()
		.Idempotent()
		.StringArg(TEXT("asset_path"), TEXT("Content path to the AnimBlueprint"), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));

			UAnimBlueprint* AnimBP = LoadObject<UAnimBlueprint>(nullptr, *AssetPath);
			if (!IsValid(AnimBP))
				return FMCPToolResult::Error(FString::Printf(TEXT("AnimBlueprint not found: %s"), *AssetPath));

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("name"), AnimBP->GetName());
			Result->SetStringField(TEXT("path"), AssetPath);

			TArray<TSharedPtr<FJsonValue>> MachineArray;

			// Walk all graphs looking for state machine nodes
			TArray<UEdGraph*> AllGraphs;
			AllGraphs.Append(AnimBP->FunctionGraphs);

			// Also check the AnimGraph
			for (UEdGraph* Graph : AnimBP->FunctionGraphs)
			{
				if (!Graph) continue;

				for (UEdGraphNode* Node : Graph->Nodes)
				{
					UAnimGraphNode_StateMachine* SMNode = Cast<UAnimGraphNode_StateMachine>(Node);
					if (!SMNode) continue;

					UAnimationStateMachineGraph* SMGraph = SMNode->EditorStateMachineGraph;
					if (!SMGraph) continue;

					TSharedPtr<FJsonObject> SMObj = MakeShared<FJsonObject>();
					// v4.6: report the BOUND GRAPH's name, which is what every
					// machine_name argument resolves against. This used to report the
					// node's display title, so the value handed back here could not
					// always be fed straight into add_anim_state / set_anim_transition_rule.
					SMObj->SetStringField(TEXT("name"), SMGraph->GetName());
					SMObj->SetStringField(TEXT("display_title"), SMNode->GetNodeTitle(ENodeTitleType::ListView).ToString());
					SMObj->SetStringField(TEXT("node_id"), SMNode->NodeGuid.ToString());

					// List states
					TArray<TSharedPtr<FJsonValue>> StateArray;
					TArray<TSharedPtr<FJsonValue>> TransitionArray;

					for (UEdGraphNode* SMNodeChild : SMGraph->Nodes)
					{
						if (UAnimStateNode* StateNode = Cast<UAnimStateNode>(SMNodeChild))
						{
							TSharedPtr<FJsonObject> StateObj = MakeShared<FJsonObject>();
							StateObj->SetStringField(TEXT("name"), StateNode->GetStateName());
							StateObj->SetStringField(TEXT("node_id"), StateNode->NodeGuid.ToString());
							StateArray.Add(MakeShared<FJsonValueObject>(StateObj));
						}
						else if (UAnimStateTransitionNode* TransNode = Cast<UAnimStateTransitionNode>(SMNodeChild))
						{
							TSharedPtr<FJsonObject> TransObj = MakeShared<FJsonObject>();
							TransObj->SetStringField(TEXT("node_id"), TransNode->NodeGuid.ToString());

							UAnimStateNodeBase* PrevState = TransNode->GetPreviousState();
							UAnimStateNodeBase* NextState = TransNode->GetNextState();
							if (PrevState)
								TransObj->SetStringField(TEXT("from"), PrevState->GetStateName());
							if (NextState)
								TransObj->SetStringField(TEXT("to"), NextState->GetStateName());

							TransitionArray.Add(MakeShared<FJsonValueObject>(TransObj));
						}
					}

					SMObj->SetNumberField(TEXT("state_count"), StateArray.Num());
					SMObj->SetArrayField(TEXT("states"), StateArray);
					SMObj->SetNumberField(TEXT("transition_count"), TransitionArray.Num());
					SMObj->SetArrayField(TEXT("transitions"), TransitionArray);

					MachineArray.Add(MakeShared<FJsonValueObject>(SMObj));
				}
			}

			Result->SetNumberField(TEXT("state_machine_count"), MachineArray.Num());
			Result->SetArrayField(TEXT("state_machines"), MachineArray);

			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});
}

} // namespace MCPAnimGraphTools::StateMachine
