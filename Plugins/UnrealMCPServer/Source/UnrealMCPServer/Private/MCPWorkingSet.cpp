// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "MCPWorkingSet.h"

TSharedPtr<FJsonObject> FMCPWorkingSet::ToJson() const
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> SelArr;
	for (const FString& S : Selection) { SelArr.Add(MakeShared<FJsonValueString>(S)); }
	Obj->SetArrayField(TEXT("selection"), SelArr);
	Obj->SetStringField(TEXT("currentBlueprintPath"), CurrentBlueprintPath);
	Obj->SetStringField(TEXT("currentWidgetPath"), CurrentWidgetPath);
	Obj->SetStringField(TEXT("currentLevelSequencePath"), CurrentLevelSequencePath);
	return Obj;
}

void FMCPWorkingSet::FromJson(const TSharedPtr<FJsonObject>& Json)
{
	if (!Json.IsValid()) return;

	// NOTE: matches the pre-refactor semantics exactly — "selection" is replaced
	// only when the caller supplies the key, while the three path fields are
	// merged via TryGetStringField (absent key leaves the existing value).
	const TArray<TSharedPtr<FJsonValue>>* SelArr = nullptr;
	if (Json->TryGetArrayField(TEXT("selection"), SelArr))
	{
		Selection.Empty();
		for (const auto& V : *SelArr) { Selection.Add(V->AsString()); }
	}
	Json->TryGetStringField(TEXT("currentBlueprintPath"), CurrentBlueprintPath);
	Json->TryGetStringField(TEXT("currentWidgetPath"), CurrentWidgetPath);
	Json->TryGetStringField(TEXT("currentLevelSequencePath"), CurrentLevelSequencePath);
}

// ============================================================================
// FMCPWorkingSetStore
// ============================================================================

FMCPWorkingSetStore& FMCPWorkingSetStore::Get()
{
	static FMCPWorkingSetStore Instance;
	return Instance;
}

FMCPWorkingSet FMCPWorkingSetStore::GetCopy(const FString& OwnerId) const
{
	FScopeLock Lock(&SetsLock);
	if (const FMCPWorkingSet* Found = SetsByOwner.Find(OwnerId))
	{
		return *Found;
	}
	return FMCPWorkingSet{};
}

void FMCPWorkingSetStore::Set(const FString& OwnerId, const FMCPWorkingSet& InSet)
{
	FScopeLock Lock(&SetsLock);
	SetsByOwner.Add(OwnerId, InSet);
}

FMCPWorkingSet FMCPWorkingSetStore::ApplyJson(const FString& OwnerId, const TSharedPtr<FJsonObject>& Json)
{
	FScopeLock Lock(&SetsLock);
	FMCPWorkingSet& WS = SetsByOwner.FindOrAdd(OwnerId);
	WS.FromJson(Json);
	return WS;
}

void FMCPWorkingSetStore::Clear(const FString& OwnerId)
{
	FScopeLock Lock(&SetsLock);
	SetsByOwner.Remove(OwnerId);
}

bool FMCPWorkingSetStore::Contains(const FString& OwnerId) const
{
	FScopeLock Lock(&SetsLock);
	return SetsByOwner.Contains(OwnerId);
}

int32 FMCPWorkingSetStore::Num() const
{
	FScopeLock Lock(&SetsLock);
	return SetsByOwner.Num();
}
