// Copyright StraySpark Studio 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Queue.h"
#include "Serialization/Archive.h"

/**
 * Phase 4 — GOTCHA G3 (docs/02_ARCHITECTURE.md §9).
 *
 * `IHttpRequest::SetResponseBodyReceiveStream` hands us an FArchive that the HTTP
 * layer writes into **from its own thread** as bytes arrive. The completion
 * delegate, by contrast, fires on the game thread. That asymmetry is easy to miss
 * and the failure mode is ugly: touching Slate or a UObject from Serialize() is a
 * race that usually survives testing and crashes on a user's machine.
 *
 * So Serialize() does exactly one thing — push bytes onto a lock-free SPSC queue.
 * No parsing, no allocation beyond the copy, no delegates, no logging.
 *
 * The queue is owned by a TSharedPtr held by both sides, because the HTTP layer
 * can outlive the request object if a response is still draining when we give up.
 */
class FMCPHttpStreamSink : public FArchive
{
public:
	using FByteQueue = TQueue<TArray<uint8>, EQueueMode::Spsc>;

	explicit FMCPHttpStreamSink(const TSharedPtr<FByteQueue>& InQueue)
		: Queue(InQueue)
	{
		SetIsSaving(true);
		SetIsPersistent(false);
	}

	//~ FArchive
	virtual void Serialize(void* V, int64 Length) override
	{
		// HTTP THREAD. Enqueue and return — nothing else is safe here.
		if (!V || Length <= 0 || !Queue.IsValid()) { return; }

		TArray<uint8> Chunk;
		Chunk.Append(static_cast<const uint8*>(V), static_cast<int32>(Length));
		Queue->Enqueue(MoveTemp(Chunk));

		TotalBytes.fetch_add(Length, std::memory_order_relaxed);
	}

	virtual FString GetArchiveName() const override { return TEXT("FMCPHttpStreamSink"); }
	//~ End FArchive

	int64 GetTotalBytes() const { return TotalBytes.load(std::memory_order_relaxed); }

private:
	TSharedPtr<FByteQueue> Queue;
	std::atomic<int64>     TotalBytes{ 0 };
};
