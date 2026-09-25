// Copyright StraySpark Studio 2026. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

class UPackage;
class UObject;
class UClass;

namespace MCPAnimGraphTools::Common
{
	bool SplitAssetPath(const FString& FullPath, FString& OutPackagePath, FString& OutAssetName);
	bool SaveNewAsset(UPackage* Package, UObject* Asset, const FString& PackagePath);
	UClass* FindClassByShortName(const FString& ClassName);
}
