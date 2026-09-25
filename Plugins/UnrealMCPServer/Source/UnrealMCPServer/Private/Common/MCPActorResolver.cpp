// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "Common/MCPActorResolver.h"

#include "Editor.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Misc/CoreDelegates.h"

namespace MCPCommon
{

namespace
{
	/** Per-process cache. One world at a time (the editor world); a different
	 *  world pointer triggers a rebuild, which also covers level switches. */
	struct FActorLabelCache
	{
		TWeakObjectPtr<UWorld> CachedWorld;
		TMap<FString, TWeakObjectPtr<AActor>> LabelToActor;
		bool bDirty = true;
		bool bDelegatesBound = false;

		void BindDelegatesOnce()
		{
			if (bDelegatesBound || !GEngine)
			{
				return;
			}
			// Any structural change just marks the cache dirty; the next lookup
			// rebuilds in one O(n) pass. Cheaper and simpler than incremental
			// upkeep, and immune to ordering bugs.
			GEngine->OnLevelActorAdded().AddLambda([](AActor*) { InvalidateActorLabelCache(); });
			GEngine->OnLevelActorDeleted().AddLambda([](AActor*) { InvalidateActorLabelCache(); });
			FCoreDelegates::OnActorLabelChanged.AddLambda([](AActor*) { InvalidateActorLabelCache(); });
			bDelegatesBound = true;
		}

		void RebuildIfNeeded(UWorld* World)
		{
			BindDelegatesOnce();
			if (!bDirty && CachedWorld.Get() == World)
			{
				return;
			}
			LabelToActor.Empty();
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				// First-with-label wins, matching the legacy scan's semantics —
				// don't overwrite an existing entry.
				if (!LabelToActor.Contains((*It)->GetActorLabel()))
				{
					LabelToActor.Add((*It)->GetActorLabel(), *It);
				}
			}
			CachedWorld = World;
			bDirty = false;
		}
	};

	FActorLabelCache GActorLabelCache;
}

void InvalidateActorLabelCache()
{
	GActorLabelCache.bDirty = true;
}

AActor* FindActorByLabel(UWorld* World, const FString& Label)
{
	if (!World)
	{
		return nullptr;
	}
	check(IsInGameThread());
	GActorLabelCache.RebuildIfNeeded(World);

	if (TWeakObjectPtr<AActor>* Found = GActorLabelCache.LabelToActor.Find(Label))
	{
		if (AActor* Actor = Found->Get())
		{
			return Actor;
		}
		// Stale weak pointer the delegates missed (e.g. GC'd actor) — rebuild once.
		InvalidateActorLabelCache();
		GActorLabelCache.RebuildIfNeeded(World);
		if (TWeakObjectPtr<AActor>* Refreshed = GActorLabelCache.LabelToActor.Find(Label))
		{
			return Refreshed->Get();
		}
	}
	return nullptr;
}

TMap<FString, AActor*> FindActorsByLabels(UWorld* World, const TSet<FString>& Labels,
	TArray<FString>* OutMissing)
{
	TMap<FString, AActor*> Result;
	if (!World)
	{
		if (OutMissing)
		{
			OutMissing->Append(Labels.Array());
		}
		return Result;
	}
	check(IsInGameThread());
	GActorLabelCache.RebuildIfNeeded(World);

	for (const FString& Label : Labels)
	{
		AActor* Actor = FindActorByLabel(World, Label);
		if (Actor)
		{
			Result.Add(Label, Actor);
		}
		else if (OutMissing)
		{
			OutMissing->Add(Label);
		}
	}
	return Result;
}

}
