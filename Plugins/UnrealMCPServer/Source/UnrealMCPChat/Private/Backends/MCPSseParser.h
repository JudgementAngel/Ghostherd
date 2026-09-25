// Copyright StraySpark Studio 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Phase 4 — Server-Sent Events framing, shared by every streaming model backend.
 *
 * Deliberately byte-oriented rather than string-oriented. HTTP chunks arrive at
 * arbitrary boundaries, which means a chunk can split:
 *   - a line, mid-JSON
 *   - a UTF-8 multi-byte sequence, mid-codepoint
 *
 * Converting each chunk to FString as it arrives corrupts the second case (the
 * classic "why is there a � in my response" bug). So bytes accumulate here and
 * are only decoded once a complete `\n`-terminated line exists.
 *
 * Pure and allocation-light so it can be exercised from an automation spec with
 * recorded provider transcripts, including deliberately nasty split points.
 */
class FMCPSseParser
{
public:
	struct FEvent
	{
		FString EventName;   // from "event:" — Anthropic always sends one
		FString Data;        // concatenated "data:" lines
	};

	/** Append raw bytes and drain whatever complete events they produced.
	 *  Safe to call with a single byte at a time. */
	void Append(const uint8* Bytes, int32 Count, TArray<FEvent>& OutEvents);
	void Append(const TArray<uint8>& Bytes, TArray<FEvent>& OutEvents);

	/** Flush a trailing event that was never terminated by a blank line — happens
	 *  when a stream is cut off. Returns false if there was nothing pending. */
	bool Finish(FEvent& OutEvent);

	void Reset();

	/** Diagnostics: bytes still held because no complete line has arrived. */
	int32 GetPendingByteCount() const { return ByteBuffer.Num(); }

private:
	void HandleLine(const FString& Line, TArray<FEvent>& OutEvents);

	TArray<uint8> ByteBuffer;

	FString PendingEventName;
	FString PendingData;
	bool    bHasPending = false;
};
