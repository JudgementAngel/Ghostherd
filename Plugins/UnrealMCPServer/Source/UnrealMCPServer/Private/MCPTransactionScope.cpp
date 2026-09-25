// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "MCPTransactionScope.h"
// LogUnrealMCP is declared in MCPProtocol.h, not in the module header — the module
// header only forward-declares the module class.
#include "MCPProtocol.h"
#include "UnrealMCPServerModule.h"
#include "Common/MCPAssetCreate.h"
#include "Editor.h"

FMCPTransactionManager& FMCPTransactionManager::Get()
{
	static FMCPTransactionManager Instance;
	return Instance;
}

FMCPTransactionManager::EResult FMCPTransactionManager::Begin(const FString& OwnerId, const FText& Description, int32& OutIndex)
{
	OutIndex = INDEX_NONE;
	if (!GEditor || !IsInGameThread())
	{
		return EResult::NoEditor;
	}

	{
		FScopeLock Lock(&OwnersLock);
		if (OwnerId.IsEmpty() || !ActiveByOwner.IsEmpty() || GEditor->IsTransactionActive())
		{
			return EResult::AlreadyOpen;
		}
	}

	const int32 CreationMark = MCPCommon::GetAssetCreationCount();
	const int32 TxIndex = GEditor->BeginTransaction(Description);
	{
		FScopeLock Lock(&OwnersLock);
		ActiveByOwner.Add(OwnerId, FOpenTransaction{TxIndex, CreationMark});
	}

	OutIndex = TxIndex;
	UE_LOG(LogUnrealMCP, Log, TEXT("Transaction begin (owner=%s, idx=%d): %s"),
		*OwnerId, TxIndex, *Description.ToString());
	return EResult::Ok;
}

FMCPTransactionManager::EResult FMCPTransactionManager::Commit(const FString& OwnerId, int32& OutIndex)
{
	OutIndex = INDEX_NONE;
	if (!GEditor || !IsInGameThread())
	{
		return EResult::NoEditor;
	}

	{
		FScopeLock Lock(&OwnersLock);
		if (FOpenTransaction* Found = ActiveByOwner.Find(OwnerId))
		{
			OutIndex = Found->Index;
			ActiveByOwner.Remove(OwnerId);
		}
	}
	if (OutIndex == INDEX_NONE)
	{
		return EResult::NotOpen;
	}

	GEditor->EndTransaction();
	UE_LOG(LogUnrealMCP, Log, TEXT("Transaction commit (owner=%s, idx=%d)"), *OwnerId, OutIndex);
	return EResult::Ok;
}

FMCPTransactionManager::EResult FMCPTransactionManager::Rollback(const FString& OwnerId, int32& OutIndex,
	TArray<FString>* OutAssetsNotRolledBack)
{
	OutIndex = INDEX_NONE;
	if (OutAssetsNotRolledBack)
	{
		OutAssetsNotRolledBack->Reset();
	}
	if (!GEditor || !IsInGameThread())
	{
		return EResult::NoEditor;
	}

	int32 CreationMark = 0;
	{
		FScopeLock Lock(&OwnersLock);
		if (FOpenTransaction* Found = ActiveByOwner.Find(OwnerId))
		{
			OutIndex     = Found->Index;
			CreationMark = Found->CreationMark;
			ActiveByOwner.Remove(OwnerId);
		}
	}
	if (OutIndex == INDEX_NONE)
	{
		return EResult::NotOpen;
	}

	GEditor->EndTransaction(); // Preserve history; cancellation is not undo.

    // No effects were reversed; report known creations without claiming completeness.
	const TArray<FString> Survivors = MCPCommon::GetAssetsCreatedSince(CreationMark);
	if (OutAssetsNotRolledBack)
	{
		*OutAssetsNotRolledBack = Survivors;
	}

	UE_LOG(LogUnrealMCP, Log, TEXT("Transaction closed without rollback (owner=%s, idx=%d, assets kept=%d)"),
		*OwnerId, OutIndex, Survivors.Num());
	return EResult::RecoveryUnsupported;
}

bool FMCPTransactionManager::IsOpen(const FString& OwnerId) const
{
	FScopeLock Lock(&OwnersLock);
	return ActiveByOwner.Contains(OwnerId);
}

int32 FMCPTransactionManager::AbandonIfOpen(const FString& OwnerId)
{
	int32 TxIndex = INDEX_NONE;
	{
		FScopeLock Lock(&OwnersLock);
		if (FOpenTransaction* Found = ActiveByOwner.Find(OwnerId))
		{
			TxIndex = Found->Index;
			ActiveByOwner.Remove(OwnerId);
		}
	}

	if (TxIndex != INDEX_NONE && GEditor)
	{
		GEditor->EndTransaction(); // Abandonment keeps recorded edits undoable.
		UE_LOG(LogUnrealMCP, Warning,
			TEXT("Closed abandoned transaction (idx=%d) of owner %s"), TxIndex, *OwnerId);
	}
	return TxIndex;
}

void FMCPTransactionManager::AbandonAll()
{
	TArray<FString> Owners;
	{
		FScopeLock Lock(&OwnersLock);
		ActiveByOwner.GenerateKeyArray(Owners);
	}
	for (const FString& Owner : Owners)
	{
		AbandonIfOpen(Owner);
	}
}

TArray<FString> FMCPTransactionManager::GetOpenOwners() const
{
	FScopeLock Lock(&OwnersLock);
	TArray<FString> Owners;
	ActiveByOwner.GenerateKeyArray(Owners);
	return Owners;
}

// ============================================================================
// FMCPScopedOwnerTransaction
// ============================================================================

FMCPScopedOwnerTransaction::FMCPScopedOwnerTransaction(const FString& InOwnerId, const FText& Description)
	: OwnerId(InOwnerId)
{
	const FMCPTransactionManager::EResult Result =
		FMCPTransactionManager::Get().Begin(OwnerId, Description, Index);
	bActive = (Result == FMCPTransactionManager::EResult::Ok);
}

FMCPScopedOwnerTransaction::~FMCPScopedOwnerTransaction()
{
	// Close on early return or exception without claiming undo. Retain history.
	if (bActive)
	{
		Rollback();
	}
}

void FMCPScopedOwnerTransaction::Commit()
{
	if (!bActive)
	{
		return;
	}
	int32 Closed = INDEX_NONE;
	FMCPTransactionManager::Get().Commit(OwnerId, Closed);
	bActive = false;
}

void FMCPScopedOwnerTransaction::Rollback()
{
	if (!bActive)
	{
		return;
	}
	int32 Closed = INDEX_NONE;
	FMCPTransactionManager::Get().Rollback(OwnerId, Closed);
	bActive = false;
}
