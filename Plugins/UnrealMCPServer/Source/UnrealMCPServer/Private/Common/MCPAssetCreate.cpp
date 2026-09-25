// Copyright StraySpark Studio 2026. All Rights Reserved.
#include "Common/MCPAssetCreate.h"

#include "MCPValidate.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"

namespace MCPCommon
{

UPackage* CreateAssetPackage(const FString& AssetPath, FString& OutPackagePath,
	FString& OutAssetName, FMCPToolResult& OutError)
{
	// Accept either a package path or a full object path.
	OutPackagePath = FPackageName::ObjectPathToPackageName(AssetPath);
	OutAssetName   = FPackageName::GetShortName(OutPackagePath);

	FMCPValidateResult Check = FMCPValidate::PackagePath(OutPackagePath);
	if (!Check.bOk) { OutError = Check.Error; return nullptr; }

	Check = FMCPValidate::AssetName(OutAssetName);
	if (!Check.bOk) { OutError = Check.Error; return nullptr; }

	// The guard. Covers both the loaded-asset assert and the unloaded-asset
	// silent overwrite; see the header for why both matter.
	Check = FMCPValidate::AssetDoesNotExist(OutPackagePath);
	if (!Check.bOk) { OutError = Check.Error; return nullptr; }

	UPackage* Package = CreatePackage(*OutPackagePath);
	if (!Package)
	{
		OutError = FMCPToolResult::ErrorStructured(EMCPError::Internal,
			FString::Printf(TEXT("Failed to create package '%s'."), *OutPackagePath));
		return nullptr;
	}

	NoteAssetCreated(OutPackagePath);
	return Package;
}

UPackage* CreateAssetPackage(const FString& AssetPath, FString& OutPackagePath,
	FString& OutAssetName, FString& OutError)
{
	FMCPToolResult Error;
	UPackage* Package = CreateAssetPackage(AssetPath, OutPackagePath, OutAssetName, Error);
	if (!Package)
	{
		OutError = Error.Content.Num() > 0 ? Error.Content[0].Text : TEXT("Failed to create asset package.");
	}
	return Package;
}

// ----------------------------------------------------------------------
// Creation journal (see header). Game thread only, so no locking.
// ----------------------------------------------------------------------
namespace
{
	/** Ring-ish buffer: keep the most recent entries, remember how many fell off
	 *  the front so markers handed out earlier stay meaningful. */
	constexpr int32 MaxJournalEntries = 1024;

	TArray<FString> GCreatedPackages;
	int32           GCreatedTrimmed = 0;
}

void NoteAssetCreated(const FString& PackagePath)
{
	if (PackagePath.IsEmpty())
	{
		return;
	}

	GCreatedPackages.Add(PackagePath);

	if (GCreatedPackages.Num() > MaxJournalEntries)
	{
		const int32 Drop = GCreatedPackages.Num() - MaxJournalEntries;
		GCreatedPackages.RemoveAt(0, Drop);
		GCreatedTrimmed += Drop;
	}
}

int32 GetAssetCreationCount()
{
	return GCreatedTrimmed + GCreatedPackages.Num();
}

TArray<FString> GetAssetsCreatedSince(int32 Mark)
{
	const int32 Start = FMath::Clamp(Mark - GCreatedTrimmed, 0, GCreatedPackages.Num());

	TArray<FString> Out;
	Out.Reserve(GCreatedPackages.Num() - Start);
	for (int32 Index = Start; Index < GCreatedPackages.Num(); ++Index)
	{
		Out.Add(GCreatedPackages[Index]);
	}
	return Out;
}

} // namespace MCPCommon
