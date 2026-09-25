// Copyright StraySpark Studio 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Phase 7 — gotcha G4: orphaned agent processes.
 *
 * The failure this exists to prevent: the editor crashes, or is killed from Task
 * Manager, while a `claude` or `node` child is running. Nothing reaps it. It sits
 * there holding a model subscription slot and a few hundred MB, and the user has
 * no idea it is there because we launched it hidden.
 *
 * Three defences, because any one of them alone leaks:
 *   1. Kill on ShutdownModule — covers the normal case.
 *   2. Kill on the editor's pre-exit delegate — covers "File ▸ Exit" paths that
 *      tear modules down in an order we do not control.
 *   3. THIS: a pid file written at spawn and swept at the next startup — the only
 *      one that covers a crash, which is exactly when it matters.
 *
 * The sweep is deliberately conservative. PIDs are recycled, so killing one from a
 * stale file could kill something entirely unrelated — a far worse bug than the
 * leak it fixes. Every record therefore stores the executable's file name, and the
 * sweep kills a PID only when the OS still reports that same name for it
 * (FPlatformProcess::GetApplicationName). A recycled PID reports a different name
 * and is left alone; the record is simply dropped.
 */
class FMCPChatPidRegistry
{
public:
	static FMCPChatPidRegistry& Get();

	/** Read the file and kill anything still alive from a previous editor run.
	 *  Call once, at module startup, before any agent is launched. */
	void SweepStaleProcesses();

	/** @param ExecutableName  file name only, e.g. "node.exe" — the recycled-PID guard. */
	void Record(uint32 ProcessId, const FString& Label, const FString& ExecutableName);
	void Forget(uint32 ProcessId);

	/** Kill everything we know about. Called from ShutdownModule and from the
	 *  editor's pre-exit delegate; safe to call twice. */
	void KillAllTracked();

	static FString GetFilePath();

private:
	FMCPChatPidRegistry() = default;

	struct FRecord
	{
		uint32  ProcessId = 0;
		/** Human-facing: "Claude Code". */
		FString Label;
		/** What the OS should still report for this PID: "node.exe". */
		FString ExecutableName;
		/** Seconds since the Unix epoch, for the log line only. */
		int64   SpawnedAtUnix = 0;
	};

	void Load();
	void Save();

	TArray<FRecord> Records;
	bool bLoaded = false;

	FCriticalSection Lock;
};
