// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "Backends/MCPSseParser.h"

void FMCPSseParser::Append(const TArray<uint8>& Bytes, TArray<FEvent>& OutEvents)
{
	Append(Bytes.GetData(), Bytes.Num(), OutEvents);
}

void FMCPSseParser::Append(const uint8* Bytes, int32 Count, TArray<FEvent>& OutEvents)
{
	if (!Bytes || Count <= 0) { return; }

	ByteBuffer.Append(Bytes, Count);

	// Consume every complete line. Anything after the last '\n' stays buffered —
	// that trailing fragment is where a split UTF-8 sequence would live.
	int32 SearchStart = 0;
	while (true)
	{
		int32 NewlineIdx = INDEX_NONE;
		for (int32 i = SearchStart; i < ByteBuffer.Num(); ++i)
		{
			if (ByteBuffer[i] == '\n') { NewlineIdx = i; break; }
		}
		if (NewlineIdx == INDEX_NONE) { break; }

		// Line is [0, NewlineIdx), minus a trailing '\r'.
		int32 LineLen = NewlineIdx;
		if (LineLen > 0 && ByteBuffer[LineLen - 1] == '\r') { --LineLen; }

		FString Line;
		if (LineLen > 0)
		{
			// NUL-terminate a scratch copy so UTF8_TO_TCHAR has a bounded string.
			TArray<uint8> LineBytes;
			LineBytes.Append(ByteBuffer.GetData(), LineLen);
			LineBytes.Add(0);
			Line = FString(UTF8_TO_TCHAR(reinterpret_cast<const ANSICHAR*>(LineBytes.GetData())));
		}

		HandleLine(Line, OutEvents);

		ByteBuffer.RemoveAt(0, NewlineIdx + 1, EAllowShrinking::No);
		SearchStart = 0;
	}
}

void FMCPSseParser::HandleLine(const FString& Line, TArray<FEvent>& OutEvents)
{
	// Blank line terminates an event.
	if (Line.IsEmpty())
	{
		if (bHasPending)
		{
			FEvent Event;
			Event.EventName = MoveTemp(PendingEventName);
			Event.Data      = MoveTemp(PendingData);
			OutEvents.Add(MoveTemp(Event));

			PendingEventName.Reset();
			PendingData.Reset();
			bHasPending = false;
		}
		return;
	}

	// ':' at column 0 is a comment / keep-alive. Providers send these to hold the
	// connection open; they are not events.
	if (Line[0] == TEXT(':'))
	{
		return;
	}

	int32 ColonIdx = INDEX_NONE;
	if (!Line.FindChar(TEXT(':'), ColonIdx))
	{
		// A bare field name with no value is legal SSE; nothing useful for us.
		return;
	}

	const FString Field = Line.Left(ColonIdx);
	FString Value = Line.Mid(ColonIdx + 1);
	// Exactly one leading space is stripped, per the SSE spec — not TrimStart(),
	// which would eat meaningful indentation inside a data payload.
	if (Value.StartsWith(TEXT(" ")))
	{
		Value.RightChopInline(1);
	}

	if (Field == TEXT("event"))
	{
		PendingEventName = Value;
		bHasPending = true;
	}
	else if (Field == TEXT("data"))
	{
		// Multiple data: lines in one event are joined with '\n'.
		if (!PendingData.IsEmpty())
		{
			PendingData.AppendChar(TEXT('\n'));
		}
		PendingData += Value;
		bHasPending = true;
	}
	// "id" and "retry" are ignored: we do not resume streams.
}

bool FMCPSseParser::Finish(FEvent& OutEvent)
{
	if (!bHasPending) { return false; }

	OutEvent.EventName = MoveTemp(PendingEventName);
	OutEvent.Data      = MoveTemp(PendingData);

	PendingEventName.Reset();
	PendingData.Reset();
	bHasPending = false;
	return true;
}

void FMCPSseParser::Reset()
{
	ByteBuffer.Reset();
	PendingEventName.Reset();
	PendingData.Reset();
	bHasPending = false;
}
