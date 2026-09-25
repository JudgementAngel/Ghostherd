// Copyright StraySpark Studio 2026. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "MCPProtocol.h"

class UPackage;

/**
 * v4.6 — one guarded entry point for "make me a package for a brand-new asset".
 *
 * Every create_* tool used to hand-roll the same three lines:
 *
 *     FString PackagePath = FPackageName::ObjectPathToPackageName(AssetPath);
 *     FString AssetName   = FPackageName::GetShortName(AssetPath);
 *     UPackage* Package   = CreatePackage(*PackagePath);
 *
 * That shape has two failure modes when the path is already taken, and both are
 * bad:
 *
 *   1. The asset is LOADED. CreatePackage hands back the live package, and the
 *      factory underneath (FKismetEditorUtilities::CreateBlueprint and friends)
 *      asserts on the name collision — a hard editor crash, not an error the
 *      caller can handle.
 *   2. The asset is on disk but NOT loaded. CreatePackage mints a fresh empty
 *      package, nothing asserts, and the subsequent SavePackage silently
 *      overwrites the existing .uasset. Quiet data loss.
 *
 * CreateAssetPackage() checks both conditions up front and returns a structured
 * AlreadyExists error instead, so a duplicate name is a retryable tool error.
 */
namespace MCPCommon
{
	/**
	 * Validate AssetPath and reserve a package for a new asset.
	 *
	 * AssetPath may be a long package name ("/Game/BP/BP_Foo") or a full object
	 * path ("/Game/BP/BP_Foo.BP_Foo"); both normalise to the same package.
	 *
	 * Returns the package on success. On failure returns nullptr and fills
	 * OutError with a structured InvalidPath / InvalidName / AlreadyExists /
	 * Internal result the handler can return as-is:
	 *
	 *     FMCPToolResult PkgErr;
	 *     FString PackagePath, AssetName;
	 *     UPackage* Package = MCPCommon::CreateAssetPackage(AssetPath, PackagePath, AssetName, PkgErr);
	 *     if (!Package) return PkgErr;
	 */
	UPackage* CreateAssetPackage(const FString& AssetPath, FString& OutPackagePath,
		FString& OutAssetName, FMCPToolResult& OutError);

	/** Same contract, for the handful of helpers that report through an FString. */
	UPackage* CreateAssetPackage(const FString& AssetPath, FString& OutPackagePath,
		FString& OutAssetName, FString& OutError);

	// ------------------------------------------------------------------
	// Creation journal
	//
	// UE object/package creation is NOT transactional: GEditor->CancelTransaction
	// reverts Modify()-recorded property and scene edits, but it does not delete
	// packages, and nothing puts a newly written .uasset back. run_tool_script
	// used to report `rolled_back: true` unconditionally, so a script that created
	// an asset and then failed told the caller the asset was gone while it was
	// still on disk — and the natural retry then hit the duplicate-name path.
	//
	// The journal exists so a caller can say truthfully what did NOT come back.
	// CreateAssetPackage records automatically; the few tools that reserve their
	// own package call NoteAssetCreated directly.
	// ------------------------------------------------------------------

	/** Record a created asset package. Game thread only. */
	void NoteAssetCreated(const FString& PackagePath);

	/** Opaque monotonic marker — take one before a batch, pass it to
	 *  GetAssetsCreatedSince afterwards. */
	int32 GetAssetCreationCount();

	/** Packages recorded since Mark, oldest first. */
	TArray<FString> GetAssetsCreatedSince(int32 Mark);
}
