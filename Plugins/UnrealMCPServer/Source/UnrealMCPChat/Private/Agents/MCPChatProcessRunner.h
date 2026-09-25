// Copyright StraySpark Studio 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Queue.h"
#include "Containers/Ticker.h"
#include "HAL/Runnable.h"

class FRunnableThread;

/**
 * Phase 7 — one child process, with line-framed output.
 *
 * WHY NOT FInteractiveProcess: its output delegate hands you whatever bytes the
 * pipe happened to return. NDJSON needs lines, and a JSON object split across two
 * callbacks silently corrupts the stream (docs/01 §4.4). So we own the reader.
 *
 * ── One pipe, not two ──────────────────────────────────────────────────────
 * FPlatformProcess::CreateProc takes a SINGLE write pipe for the child, and the
 * platform layer points both stdout and stderr at it. There is no portable way to
 * separate them. So the agent's diagnostics arrive interleaved with its protocol
 * lines, and the consumer MUST tolerate non-JSON lines rather than treating them
 * as protocol errors. A `node` deprecation warning on stderr is not a malformed
 * message, and an agent that prints one must still work.
 *
 * Threading:
 *   reader thread   blocking pipe read → split on '\n' → SPSC queue
 *   game thread     ticker drains the queue → OnLine
 *   game thread     WriteLine, guarded, because two turns can overlap
 *
 * Nothing on the reader thread touches Slate, UObjects or the consumer side of
 * the queue — the same discipline as gotcha G3 on the HTTP side, for the same
 * reason.
 */
class FMCPChatProcessRunner : public TSharedFromThis<FMCPChatProcessRunner>
{
public:
	DECLARE_DELEGATE_OneParam(FOnLine, const FString& /*Line*/);
	DECLARE_DELEGATE_TwoParams(FOnExited, int32 /*ReturnCode*/, bool /*bWasRequested*/);

	FMCPChatProcessRunner();
	~FMCPChatProcessRunner();

	struct FLaunchParams
	{
		/** Command name or absolute path — resolved through ResolveExecutable. */
		FString Executable;
		FString Arguments;
		FString WorkingDirectory;
		/** Applied around the spawn — see the note in Launch(). */
		TMap<FString, FString> Environment;
		/** Label used in logs and in the PID registry. */
		FString Label;
	};

	/** @return false with OutError set. Does not throw; a failed spawn is a normal
	 *  outcome (the binary is not installed). */
	bool Launch(const FLaunchParams& Params, FText& OutError);

	/** Newline-framed write to the child's stdin. Game thread. */
	bool WriteLine(const FString& Line);

	/**
	 * Ask the child to exit, then insist.
	 *
	 * @param GraceSeconds  how long to wait for a clean exit before TerminateProc.
	 *                      Zero kills immediately; the ACP path passes 3 s after a
	 *                      session/cancel so a well-behaved agent can finish writing.
	 */
	void Stop(float GraceSeconds = 3.f);

	bool IsRunning() const;
	uint32 GetProcessId() const { return ProcessId; }

	FOnLine   OnLine;
	FOnExited OnExited;

	/**
	 * Find an executable the way a shell would.
	 *
	 * UE has no portable `which`, and agents are installed as `claude`, `npx`,
	 * `gemini` — names, not paths. An absolute path is used as-is; anything else is
	 * probed against PATH, with the Windows executable extensions appended, because
	 * `npx` on Windows is `npx.cmd` and CreateProc will not find it otherwise.
	 *
	 * @return the resolved absolute path, or empty when nothing matched.
	 */
	static FString ResolveExecutable(const FString& CommandOrPath);

private:
	/** Blocking pipe reader. Its whole job is bytes → lines → queue. */
	class FReaderRunnable : public FRunnable
	{
	public:
		FReaderRunnable(void* InPipe, TQueue<FString, EQueueMode::Spsc>* InQueue)
			: Pipe(InPipe), Queue(InQueue) {}

		virtual uint32 Run() override;
		virtual void   Stop() override { bRequestedStop = true; }

	private:
		void* Pipe = nullptr;
		TQueue<FString, EQueueMode::Spsc>* Queue = nullptr;
		FThreadSafeBool bRequestedStop = false;

		/** Carries a partial line between reads. The entire point of this class. */
		FString Partial;
	};

	bool Tick(float DeltaTime);
	void CloseHandles();
	void HandleExit(bool bWasRequested);

	FProcHandle ProcessHandle;
	uint32      ProcessId = 0;

	void* OutRead   = nullptr;
	void* OutWrite  = nullptr;
	void* StdInRead = nullptr;
	void* StdInWrite = nullptr;

	TQueue<FString, EQueueMode::Spsc> OutQueue;

	TUniquePtr<FReaderRunnable> Reader;
	FRunnableThread*            ReaderThread = nullptr;

	bool bStopRequested = false;

	/** Set when Stop() started a grace period; the ticker enforces the deadline. */
	double TerminateDeadline = 0.0;

	/** True only while destructing — the owner cannot receive callbacks any more. */
	bool bSuppressExitCallback = false;

	FCriticalSection WriteLock;
	FTSTicker::FDelegateHandle TickerHandle;

	FString Label;

	/** Frame budget for draining, matching the HTTP stream sink's. A chatty agent
	 *  must not be able to stall the editor by writing faster than we read. */
	static constexpr double DrainBudgetSeconds = 0.004;
};
