// Copyright StraySpark Studio 2026. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

class UWorld;
class AActor;

/**
 * v4 Phase 1 — cached actor-by-label resolution.
 *
 * v3 redefined an O(n) TActorIterator scan in 43+ places; a batch op over 100
 * actors in a 10k-actor level cost 1M iterations. This resolver keeps a
 * label -> actor map per world, invalidated by the engine's actor-added /
 * actor-deleted / label-changed delegates, and rebuilds lazily on first use
 * after a change. Lookups are O(1) amortized; correctness matches the old
 * scan exactly (first actor with a matching label wins).
 *
 * Game-thread only (like all editor actor access).
 */
namespace MCPCommon
{
	/** O(1) amortized replacement for the per-file FindActorByLabel scans. */
	AActor* FindActorByLabel(UWorld* World, const FString& Label);

	/** Resolve a batch of labels in one pass. Missing labels (no actor) are
	 *  appended to OutMissing when provided. Returned map contains only hits. */
	TMap<FString, AActor*> FindActorsByLabels(UWorld* World, const TSet<FString>& Labels,
		TArray<FString>* OutMissing = nullptr);

	/** Force the cache dirty (e.g. after bulk spawning outside the editor delegates). */
	void InvalidateActorLabelCache();
}
