// Copyright StraySpark Studio 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Phase 0 / R1 — shared multi-call transaction manager.
 *
 * Extracted from FMCPHttpServer so more than one front-end can collapse a
 * sequence of mutations into a single editor undo step. The HTTP server keys
 * transactions by Mcp-Session-Id; the embedded chat panel keys them by chat
 * session GUID. Both go through here, so a stale owner from either front-end is
 * swept the same way and the undo stack can never be left with an orphaned open
 * transaction.
 *
 * Threading: every method must be called from the GAME THREAD — GEditor's
 * transaction API is not thread-safe. The internal map is still lock-guarded
 * because owner bookkeeping is read from the HTTP sweep ticker and (later) the
 * chat controller, and cheap locking here costs nothing next to a transaction.
 */
class UNREALMCPSERVER_API FMCPTransactionManager
{
public:
	enum class EResult : uint8
	{
		Ok,
		NoEditor,       // GEditor unavailable (commandlet / cook)
		AlreadyOpen,    // this owner already has an open transaction
		NotOpen,        // this owner has no open transaction
        RecoveryUnsupported, // transaction closed, history retained; no automatic undo
	};

	static FMCPTransactionManager& Get();

	/** Open a transaction for OwnerId. OutIndex receives the UE transaction index. */
	EResult Begin(const FString& OwnerId, const FText& Description, int32& OutIndex);

	/** Close and keep the owner's transaction. OutIndex receives the index that was closed. */
	EResult Commit(const FString& OwnerId, int32& OutIndex);

    /** Legacy close-on-failure API. Retains edits and undo history, returns RecoveryUnsupported.
     * OutAssetsNotRolledBack reports only journaled creations, not all possible effects. */
	EResult Rollback(const FString& OwnerId, int32& OutIndex,
		TArray<FString>* OutAssetsNotRolledBack = nullptr);

	bool IsOpen(const FString& OwnerId) const;

	/**
	 * Close and forget, preserving undo history for any transaction held by OwnerId. Used by session GC and
	 * on shutdown — an abandoned open transaction corrupts the undo stack for the
	 * rest of the editor session. Returns the closed index, or INDEX_NONE if
	 * the owner had nothing open.
	 */
	int32 AbandonIfOpen(const FString& OwnerId);

	/** Close every open transaction, preserving history. Called on module shutdown. */
	void AbandonAll();

	/** Owner ids with an open transaction (diagnostics / status UI). */
	TArray<FString> GetOpenOwners() const;

private:
	FMCPTransactionManager() = default;

	/** Per-owner open transaction: the UE transaction index plus the asset-creation
	 *  journal marker taken when it opened. */
	struct FOpenTransaction
	{
		int32 Index        = INDEX_NONE;
		int32 CreationMark = 0;
	};

	TMap<FString, FOpenTransaction> ActiveByOwner;
	mutable FCriticalSection OwnersLock;
};

/**
 * RAII wrapper for the common "one turn = one undo step" case.
 *
 *   {
 *       FMCPScopedOwnerTransaction Txn(SessionGuid.ToString(), LOCTEXT("ChatTurn", "MCP Chat Turn"));
 *       ... run tools ...
 *       Txn.Commit();          // omit to close without automatic undo
 *   }
 *
 * Destructing without Commit() closes while retaining changes and history; an early return or exception
 * cannot leave the transaction open.
 */
class UNREALMCPSERVER_API FMCPScopedOwnerTransaction
{
public:
	FMCPScopedOwnerTransaction(const FString& InOwnerId, const FText& Description);
	~FMCPScopedOwnerTransaction();

	FMCPScopedOwnerTransaction(const FMCPScopedOwnerTransaction&) = delete;
	FMCPScopedOwnerTransaction& operator=(const FMCPScopedOwnerTransaction&) = delete;

	/** True when the transaction actually opened (false when GEditor was unavailable
	 *  or the owner already had one open — in both cases this scope is a no-op). */
	bool IsActive() const { return bActive; }

	int32 GetIndex() const { return Index; }

	/** Commit now; the destructor then does nothing. */
	void Commit();

	/** Close without automatic undo; the destructor then does nothing. */
	void Rollback();

private:
	FString OwnerId;
	int32   Index = INDEX_NONE;
	bool    bActive = false;
};
