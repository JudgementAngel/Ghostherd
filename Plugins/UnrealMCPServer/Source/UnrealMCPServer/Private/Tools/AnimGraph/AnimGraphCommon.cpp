// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "Tools/AnimGraph/AnimGraphCommon.h"

#include "MCPToolRegistry.h"
#include "MCPProtocol.h"

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

namespace MCPAnimGraphTools::Common
{

// ============================================================================
// Helper: create package path and asset name from a full content path
// ============================================================================

bool SplitAssetPath(const FString& FullPath, FString& OutPackagePath, FString& OutAssetName)
{
	OutPackagePath = FPackageName::ObjectPathToPackageName(FullPath);
	OutAssetName = FPackageName::GetShortName(FullPath);
	return !OutPackagePath.IsEmpty() && !OutAssetName.IsEmpty();
}

// ============================================================================
// Helper: save a newly created asset package
// ============================================================================

bool SaveNewAsset(UPackage* Package, UObject* Asset, const FString& PackagePath)
{
	FAssetRegistryModule::AssetCreated(Asset);
	Package->MarkPackageDirty();

	const FString PackageFilename = FPackageName::LongPackageNameToFilename(
		PackagePath, FPackageName::GetAssetPackageExtension());

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	return UPackage::SavePackage(Package, Asset, *PackageFilename, SaveArgs);
}

// ============================================================================
// Helper: find a UClass by short name, trying several lookup strategies
// ============================================================================

UClass* FindClassByShortName(const FString& ClassName)
{
	// Exact match first
	UClass* Found = FindFirstObject<UClass>(*ClassName, EFindFirstObjectOptions::ExactClass);
	if (Found) return Found;

	// Try with U prefix
	Found = FindFirstObject<UClass>(*FString::Printf(TEXT("U%s"), *ClassName), EFindFirstObjectOptions::ExactClass);
	if (Found) return Found;

	// Try LoadClass from common modules
	const TArray<FString> Modules = { TEXT("Engine"), TEXT("AnimGraphRuntime"), TEXT("GameplayAbilities") };
	for (const FString& Module : Modules)
	{
		Found = LoadClass<UObject>(nullptr, *FString::Printf(TEXT("/Script/%s.%s"), *Module, *ClassName));
		if (Found) return Found;
	}

	return nullptr;
}

} // namespace MCPAnimGraphTools::Common
