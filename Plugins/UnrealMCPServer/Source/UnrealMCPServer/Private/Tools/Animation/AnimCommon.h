// Copyright StraySpark Studio 2026. All Rights Reserved.
//
// v4.6 — shared helper layer for the animation tool families.
//
// Every helper here exists because the same check was being hand-rolled (or,
// more often, skipped) across the v4.5 animation tools. In particular the
// engine's animation edit APIs are overwhelmingly *silent* on bad input:
// UAnimationBlueprintLibrary::AddAnimationNotifyEvent logs a warning and adds
// nothing when the notify track name does not exist, UBlendSpace::AddSample
// returns INDEX_NONE, USkeleton slot registration is a no-op for an unknown
// group. Pre-validating here is what turns those into structured errors an
// agent can actually recover from.

#pragma once

#include "CoreMinimal.h"
#include "MCPProtocol.h"

class UAnimBlueprint;
class UAnimMontage;
class UAnimSequence;
class UAnimSequenceBase;
class UAnimationAsset;
class UAnimationStateMachineGraph;
class UAnimStateNode;
class UAnimStateTransitionNode;
class UBlendProfile;
class UEdGraph;
class UEdGraphNode;
class UEdGraphPin;
class UPackage;
class USkeleton;

namespace MCPAnimTools::Common
{
	// ---------------------------------------------------------------------
	// Asset plumbing
	// ---------------------------------------------------------------------

	/** Split "/Game/Folder/Asset" into package path and short name. */
	bool SplitAssetPath(const FString& FullPath, FString& OutPackagePath, FString& OutAssetName);

	/** Save the package that already owns Asset (used after an in-place edit).
	 *  Returns false and fills OutError when the package cannot be written. */
	bool SaveExistingAsset(UObject* Asset, FMCPToolResult& OutError);

	/** Resolve a UClass from a short or fully-qualified name, restricted to
	 *  children of BaseClass. Returns nullptr and fills OutError (with
	 *  did-you-mean suggestions drawn from loaded subclasses) on failure. */
	UClass* ResolveClass(const FString& ClassName, UClass* BaseClass, FMCPToolResult& OutError);

	// ---------------------------------------------------------------------
	// Skeleton compatibility
	// ---------------------------------------------------------------------

	/** Assert that Asset targets Expected. Most animation tools take two assets
	 *  that must agree on a skeleton; the engine silently produces garbage when
	 *  they do not. Names both skeletons in the error. */
	bool RequireSameSkeleton(const USkeleton* Expected, const UAnimationAsset* Asset,
		const TCHAR* AssetLabel, FMCPToolResult& OutError);

	// ---------------------------------------------------------------------
	// Animation sequence / montage editing
	// ---------------------------------------------------------------------

	/** Assert 0 <= Time <= PlayLength. */
	bool RequireTimeInRange(const UAnimSequenceBase* Anim, double Time,
		const TCHAR* FieldName, FMCPToolResult& OutError);

	/** Resolve a notify track by name. When bCreateIfMissing the track is added
	 *  (UAnimationBlueprintLibrary::AddAnimationNotifyTrack); otherwise a
	 *  NotFound error listing the existing track names is produced. */
	bool ResolveNotifyTrack(UAnimSequenceBase* Anim, const FString& TrackName,
		bool bCreateIfMissing, FName& OutTrackName, FMCPToolResult& OutError);

	/** Open an edit on an animation asset: Modify() so the registry transaction
	 *  (and run_tool_script rollback) can revert it. */
	void BeginAnimEdit(UAnimSequenceBase* Anim);

	/** Close an edit: refresh derived notify/marker caches and dirty the package.
	 *  Deliberately does NOT save — the registry owns the transaction and the
	 *  agent owns when to persist. */
	void EndAnimEdit(UAnimSequenceBase* Anim);

	/** Resolve a montage section by name to its index, or a NotFound error
	 *  listing the montage's actual section names. */
	bool ResolveSection(UAnimMontage* Montage, const FString& SectionName,
		int32& OutIndex, FMCPToolResult& OutError);

	/** Resolve a montage slot track by name to its index. */
	bool ResolveSlot(UAnimMontage* Montage, const FString& SlotName,
		int32& OutIndex, FMCPToolResult& OutError);

	/** Serialize a montage to the shape shared by get_anim_montage_info,
	 *  montage_get_sections and montage_validate. */
	TSharedPtr<FJsonObject> MontageToJson(UAnimMontage* Montage, bool bIncludeNotifies);

	// ---------------------------------------------------------------------
	// AnimBlueprint graph navigation
	// ---------------------------------------------------------------------

	/** Find the AnimBP's "AnimGraph" (it lives in FunctionGraphs, not
	 *  UbergraphPages). */
	UEdGraph* FindAnimGraph(UAnimBlueprint* AnimBP);

	/** Find a state machine's inner graph by machine name. On failure fills
	 *  OutError with the list of machines that do exist. */
	UAnimationStateMachineGraph* FindStateMachineGraph(UAnimBlueprint* AnimBP,
		const FString& MachineName, FMCPToolResult& OutError);

	/** Find a state node by name within a state machine graph. */
	UAnimStateNode* FindState(UAnimationStateMachineGraph* SMGraph,
		const FString& StateName, FMCPToolResult& OutError);

	/** Find the transition node linking two states. */
	UAnimStateTransitionNode* FindTransition(UAnimationStateMachineGraph* SMGraph,
		const FString& FromState, const FString& ToState, FMCPToolResult& OutError);

	/** First pose pin on a node in the given direction. When PreferredName is
	 *  non-empty that pin is required instead (used for multi-input nodes such
	 *  as LayeredBoneBlend, whose inputs are "BasePose"/"BlendPoses_0"). */
	UEdGraphPin* FindPosePin(UEdGraphNode* Node, EEdGraphPinDirection Direction,
		const FString& PreferredName = FString());

	/** Human-readable list of a node's pose pins, for error hints. */
	FString DescribePosePins(UEdGraphNode* Node);

	/** Compile the AnimBP and report diagnostics. bSave persists the package.
	 *  Always fills OutResult with {compiled, error_count, warning_count,
	 *  messages[]}; returns false when compilation produced errors. */
	bool CompileAnimBlueprint(UAnimBlueprint* AnimBP, bool bSave, TSharedPtr<FJsonObject>& OutResult);

	/** Resolve a blend profile by name on a skeleton. */
	UBlendProfile* ResolveBlendProfile(USkeleton* Skeleton, const FString& ProfileName,
		FMCPToolResult& OutError);
}
