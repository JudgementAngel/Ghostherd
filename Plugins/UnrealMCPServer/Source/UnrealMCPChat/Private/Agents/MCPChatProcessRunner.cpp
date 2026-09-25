// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "Agents/MCPChatProcessRunner.h"
#include "Agents/MCPChatPidRegistry.h"
#include "UnrealMCPChatModule.h"

#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/RunnableThread.h"
#include "Misc/Paths.h"

#define LOCTEXT_NAMESPACE "MCPChatProcessRunner"

namespace
{
	/** Extensions to try when the command has none. On Windows the agent CLIs are
	 *  almost always .cmd shims from npm, and CreateProc will not find "npx"
	 *  without one. */
	const TCHAR* WindowsExtensions[] = { TEXT(".exe"), TEXT(".cmd"), TEXT(".bat"), TEXT(".com") };

	/** A single "line" past this is a runaway, not a message. */
	constexpr int32 MaxPartialBytes = 16 * 1024 * 1024;
}

// ============================================================================
// Reader thread
// ============================================================================

uint32 FMCPChatProcessRunner::FReaderRunnable::Run()
{
	while (!bRequestedStop)
	{
		const FString Chunk = FPlatformProcess::ReadPipe(Pipe);
		if (Chunk.IsEmpty())
		{
			// Nothing available. Sleeping rather than spinning: this thread exists to
			// wait, and a busy loop costs a core for the life of the agent.
			FPlatformProcess::Sleep(0.01f);
			continue;
		}

		Partial.Append(Chunk);

		// Split on '\n'. A JSON object split across two reads is the failure this
		// class exists to prevent, so the remainder is carried, never emitted.
		int32 Newline = INDEX_NONE;
		while (Partial.FindChar(TEXT('\n'), Newline))
		{
			FString Line = Partial.Left(Newline);
			Partial.RightChopInline(Newline + 1, EAllowShrinking::No);

			// Tolerate CRLF: a Windows shim in the middle of the pipe produces it, and
			// a trailing '\r' makes every JSON parse fail in a way that reads like a
			// protocol bug.
			Line.RemoveFromEnd(TEXT("\r"));

			if (!Line.IsEmpty())
			{
				Queue->Enqueue(MoveTemp(Line));
			}
		}

		if (Partial.Len() > MaxPartialBytes)
		{
			Partial.Reset();
		}
	}

	return 0;
}

// ============================================================================
// Lifetime
// ============================================================================

FMCPChatProcessRunner::FMCPChatProcessRunner()
{
}

FMCPChatProcessRunner::~FMCPChatProcessRunner()
{
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}

	// No grace period in a destructor: whoever owned this is already gone, and
	// waiting three seconds inside editor teardown is worse than a hard kill.
	//
	// OnExited is suppressed for the same reason — the owner is mid-destruction, and
	// calling back into it is how a clean shutdown becomes a crash report.
	bSuppressExitCallback = true;
	Stop(0.f);
	CloseHandles();
}

FString FMCPChatProcessRunner::ResolveExecutable(const FString& CommandOrPath)
{
	if (CommandOrPath.IsEmpty()) { return FString(); }

	IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
	auto Exists = [&PF](const FString& Path) { return PF.FileExists(*Path); };

	auto TryWithExtensions = [&Exists](const FString& Base) -> FString
	{
#if PLATFORM_WINDOWS
		// Extensions FIRST, exactly as PATHEXT works.
		//
		// npm installs BOTH `npx` (a bash shim with no extension) and `npx.cmd` into
		// the same directory. Checking the bare name first finds the bash script,
		// which CreateProc cannot launch — the failure reads "Could not start
		// C:\Program Files\nodejs\npx" and gives no hint that the real executable
		// is sitting right beside it.
		if (FPaths::GetExtension(Base).IsEmpty())
		{
			for (const TCHAR* Ext : WindowsExtensions)
			{
				const FString Candidate = Base + Ext;
				if (Exists(Candidate)) { return Candidate; }
			}
		}
#endif
		if (Exists(Base)) { return Base; }
		return FString();
	};

	// Anything with a separator, or an absolute path, is taken at face value.
	const bool bLooksLikePath = !FPaths::IsRelative(CommandOrPath)
		|| CommandOrPath.Contains(TEXT("/"))
		|| CommandOrPath.Contains(TEXT("\\"));

	if (bLooksLikePath)
	{
		return TryWithExtensions(FPaths::ConvertRelativePathToFull(CommandOrPath));
	}

	// A bare name: walk PATH the way a shell would.
	const FString PathVar = FPlatformMisc::GetEnvironmentVariable(TEXT("PATH"));
	if (PathVar.IsEmpty()) { return FString(); }

#if PLATFORM_WINDOWS
	const TCHAR* Separator = TEXT(";");
#else
	const TCHAR* Separator = TEXT(":");
#endif

	TArray<FString> Directories;
	PathVar.ParseIntoArray(Directories, Separator, true);

	for (const FString& Dir : Directories)
	{
		if (Dir.IsEmpty()) { continue; }
		const FString Found = TryWithExtensions(Dir / CommandOrPath);
		if (!Found.IsEmpty()) { return Found; }
	}

	return FString();
}

bool FMCPChatProcessRunner::Launch(const FLaunchParams& Params, FText& OutError)
{
	check(IsInGameThread());

	if (IsRunning())
	{
		OutError = LOCTEXT("AlreadyRunning", "That agent is already running.");
		return false;
	}

	Label = Params.Label.IsEmpty() ? FPaths::GetCleanFilename(Params.Executable) : Params.Label;

	const FString Executable = ResolveExecutable(Params.Executable);
	if (Executable.IsEmpty())
	{
		OutError = FText::Format(
			LOCTEXT("NotOnPath", "'{0}' was not found on PATH. Install it, or set an absolute path in Settings."),
			FText::FromString(Params.Executable));
		return false;
	}

	// Two pipes: one for everything the child writes (see the header note on why
	// stdout and stderr cannot be separated), one for its stdin. The child gets the
	// READ end of stdin; we keep the write end, hence bWritePipeLocal.
	if (!FPlatformProcess::CreatePipe(OutRead, OutWrite)
		|| !FPlatformProcess::CreatePipe(StdInRead, StdInWrite, /*bWritePipeLocal*/ true))
	{
		CloseHandles();
		OutError = LOCTEXT("PipeFailed", "Could not create pipes for the agent process.");
		return false;
	}

	// Environment: CreateProc has no per-child environment parameter, so these are
	// set on OUR process around the spawn and restored immediately after. Not
	// elegant, and not safe against a concurrent spawn — which is why agent launches
	// are game-thread only.
	TMap<FString, FString> Saved;
	for (const TPair<FString, FString>& Pair : Params.Environment)
	{
		Saved.Add(Pair.Key, FPlatformMisc::GetEnvironmentVariable(*Pair.Key));
		FPlatformMisc::SetEnvironmentVar(*Pair.Key, *Pair.Value);
	}

	const FString WorkingDir = Params.WorkingDirectory.IsEmpty()
		? FPaths::ConvertRelativePathToFull(FPaths::ProjectDir())
		: Params.WorkingDirectory;

	ProcessHandle = FPlatformProcess::CreateProc(
		*Executable,
		*Params.Arguments,
		/*bLaunchDetached*/     false,
		/*bLaunchHidden*/       true,
		/*bLaunchReallyHidden*/ true,
		&ProcessId,
		/*PriorityModifier*/    0,
		*WorkingDir,
		OutWrite,
		StdInRead);

	for (const TPair<FString, FString>& Pair : Saved)
	{
		FPlatformMisc::SetEnvironmentVar(*Pair.Key, *Pair.Value);
	}

	if (!ProcessHandle.IsValid())
	{
		CloseHandles();
		OutError = FText::Format(LOCTEXT("SpawnFailed", "Could not start '{0}'."),
			FText::FromString(Executable));
		return false;
	}

	// Gotcha G4: record the PID immediately. An editor crash three lines from here
	// must still leave a reapable record. The executable's file name goes in too —
	// it is what stops a later sweep from killing a recycled PID.
	FMCPChatPidRegistry::Get().Record(ProcessId, Label, FPaths::GetCleanFilename(Executable));

	bStopRequested = false;
	TerminateDeadline = 0.0;

	Reader = MakeUnique<FReaderRunnable>(OutRead, &OutQueue);
	ReaderThread = FRunnableThread::Create(Reader.Get(),
		*FString::Printf(TEXT("MCPChatAgent_%u"), ProcessId), 0, TPri_BelowNormal);

	if (!TickerHandle.IsValid())
	{
		TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateSP(this, &FMCPChatProcessRunner::Tick));
	}

	UE_LOG(LogUnrealMCPChat, Log, TEXT("Agent '%s' started (pid %u): %s %s"),
		*Label, ProcessId, *Executable, *Params.Arguments);

	return true;
}

bool FMCPChatProcessRunner::IsRunning() const
{
	return ProcessHandle.IsValid()
		&& FPlatformProcess::IsProcRunning(const_cast<FProcHandle&>(ProcessHandle));
}

bool FMCPChatProcessRunner::WriteLine(const FString& Line)
{
	if (!StdInWrite || !ProcessHandle.IsValid()) { return false; }

	// The byte overload with an explicit '\n', rather than the FString overload:
	// that one appends a newline itself on some platforms, and a newline-framed
	// protocol cannot afford the ambiguity.
	FTCHARToUTF8 Converter(*Line);
	TArray<uint8> Bytes;
	Bytes.Reserve(Converter.Length() + 1);
	Bytes.Append(reinterpret_cast<const uint8*>(Converter.Get()), Converter.Length());
	Bytes.Add(static_cast<uint8>('\n'));

	FScopeLock Lock(&WriteLock);
	return FPlatformProcess::WritePipe(StdInWrite, Bytes.GetData(), Bytes.Num(), nullptr);
}

void FMCPChatProcessRunner::Stop(float GraceSeconds)
{
	if (!ProcessHandle.IsValid()) { return; }

	bStopRequested = true;

	if (GraceSeconds > 0.f && FPlatformProcess::IsProcRunning(ProcessHandle))
	{
		// Closing stdin is the polite signal: a CLI reading a stream sees EOF and
		// shuts itself down, flushing whatever it was mid-way through. The ticker
		// enforces the deadline if it does not.
		if (StdInRead || StdInWrite)
		{
			FPlatformProcess::ClosePipe(StdInRead, StdInWrite);
			StdInRead = nullptr;
			StdInWrite = nullptr;
		}
		TerminateDeadline = FPlatformTime::Seconds() + GraceSeconds;
		return;
	}

	// Kill the TREE: agents spawn node, python and their own subprocesses, and
	// killing only the parent is precisely how orphans are created (gotcha G4).
	if (FPlatformProcess::IsProcRunning(ProcessHandle))
	{
		FPlatformProcess::TerminateProc(ProcessHandle, /*KillTree*/ true);
	}

	HandleExit(/*bWasRequested*/ true);
}

void FMCPChatProcessRunner::HandleExit(bool bWasRequested)
{
	// Drain what is left first: an agent that dies on a startup error writes the
	// reason and then exits, and reporting "exited" without that line makes the
	// failure unexplainable.
	FString Line;
	while (OutQueue.Dequeue(Line))
	{
		OnLine.ExecuteIfBound(Line);
	}

	int32 ReturnCode = 0;
	if (ProcessHandle.IsValid())
	{
		FPlatformProcess::GetProcReturnCode(ProcessHandle, &ReturnCode);
		FPlatformProcess::CloseProc(ProcessHandle);
		ProcessHandle.Reset();
	}

	if (ReaderThread)
	{
		// Kill(true) waits for Run() to return. The reader polls its stop flag every
		// 10 ms at worst, so this is bounded.
		ReaderThread->Kill(true);
		delete ReaderThread;
		ReaderThread = nullptr;
	}
	Reader.Reset();

	if (ProcessId != 0)
	{
		FMCPChatPidRegistry::Get().Forget(ProcessId);
		ProcessId = 0;
	}

	TerminateDeadline = 0.0;

	if (!bSuppressExitCallback)
	{
		OnExited.ExecuteIfBound(ReturnCode, bWasRequested);
	}
}

bool FMCPChatProcessRunner::Tick(float /*DeltaTime*/)
{
	// ---- Drain, on a budget ----
	const double Start = FPlatformTime::Seconds();

	FString Line;
	while (OutQueue.Dequeue(Line))
	{
		OnLine.ExecuteIfBound(Line);
		if (FPlatformTime::Seconds() - Start > DrainBudgetSeconds) { break; }
	}

	// ---- Grace period expired ----
	if (TerminateDeadline > 0.0 && FPlatformTime::Seconds() >= TerminateDeadline)
	{
		TerminateDeadline = 0.0;
		if (ProcessHandle.IsValid() && FPlatformProcess::IsProcRunning(ProcessHandle))
		{
			UE_LOG(LogUnrealMCPChat, Warning,
				TEXT("Agent '%s' did not exit within its grace period; terminating."), *Label);
			FPlatformProcess::TerminateProc(ProcessHandle, /*KillTree*/ true);
		}
		HandleExit(/*bWasRequested*/ true);
		return true;
	}

	// ---- Died on its own ----
	if (ProcessHandle.IsValid() && !FPlatformProcess::IsProcRunning(ProcessHandle))
	{
		HandleExit(bStopRequested);
	}

	return true;
}

void FMCPChatProcessRunner::CloseHandles()
{
	if (OutRead || OutWrite)      { FPlatformProcess::ClosePipe(OutRead, OutWrite); }
	if (StdInRead || StdInWrite)  { FPlatformProcess::ClosePipe(StdInRead, StdInWrite); }

	OutRead = OutWrite = nullptr;
	StdInRead = StdInWrite = nullptr;
}

#undef LOCTEXT_NAMESPACE
