// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "Agents/MCPChatPidRegistry.h"
#include "MCPChatStore.h"
#include "UnrealMCPChatModule.h"

#include "Dom/JsonObject.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

FMCPChatPidRegistry& FMCPChatPidRegistry::Get()
{
	static FMCPChatPidRegistry Instance;
	return Instance;
}

FString FMCPChatPidRegistry::GetFilePath()
{
	return FMCPChatStore::GetRootDirectory() / TEXT("pids.json");
}

void FMCPChatPidRegistry::Load()
{
	if (bLoaded) { return; }
	bLoaded = true;

	FString Raw;
	if (!FFileHelper::LoadFileToString(Raw, *GetFilePath())) { return; }

	TArray<TSharedPtr<FJsonValue>> Array;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Raw);
	if (!FJsonSerializer::Deserialize(Reader, Array)) { return; }

	for (const TSharedPtr<FJsonValue>& Value : Array)
	{
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (!Value.IsValid() || !Value->TryGetObject(Obj) || !Obj) { continue; }

		FRecord R;
		double Pid = 0.0;
		if (!(*Obj)->TryGetNumberField(TEXT("pid"), Pid)) { continue; }
		R.ProcessId = static_cast<uint32>(Pid);

		(*Obj)->TryGetStringField(TEXT("label"), R.Label);
		(*Obj)->TryGetStringField(TEXT("exe"), R.ExecutableName);

		double Spawned = 0.0;
		(*Obj)->TryGetNumberField(TEXT("spawnedAt"), Spawned);
		R.SpawnedAtUnix = static_cast<int64>(Spawned);

		Records.Add(MoveTemp(R));
	}
}

void FMCPChatPidRegistry::Save()
{
	TArray<TSharedPtr<FJsonValue>> Array;
	for (const FRecord& R : Records)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetNumberField(TEXT("pid"), R.ProcessId);
		Obj->SetStringField(TEXT("label"), R.Label);
		Obj->SetStringField(TEXT("exe"), R.ExecutableName);
		Obj->SetNumberField(TEXT("spawnedAt"), static_cast<double>(R.SpawnedAtUnix));
		Array.Add(MakeShared<FJsonValueObject>(Obj));
	}

	FString Out;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Array, Writer);

	// Written immediately, not debounced. The whole point is to survive a crash that
	// happens one instruction after the spawn.
	FFileHelper::SaveStringToFile(Out, *GetFilePath());
}

void FMCPChatPidRegistry::SweepStaleProcesses()
{
	FScopeLock ScopeLock(&Lock);
	Load();

	if (Records.Num() == 0) { return; }

	int32 Killed = 0;
	int32 Skipped = 0;

	for (const FRecord& R : Records)
	{
		if (R.ProcessId == 0) { continue; }

		FProcHandle Handle = FPlatformProcess::OpenProcess(R.ProcessId);
		if (!Handle.IsValid()) { continue; }   // already gone

		const bool bStillRunning = FPlatformProcess::IsProcRunning(Handle);
		if (!bStillRunning)
		{
			FPlatformProcess::CloseProc(Handle);
			continue;
		}

		// The recycled-PID guard. Without it, a pid file from three days ago could
		// name a PID the OS has since handed to the user's own editor session.
		const FString CurrentName = FPaths::GetCleanFilename(
			FPlatformProcess::GetApplicationName(R.ProcessId));

		const bool bNameMatches = !R.ExecutableName.IsEmpty()
			&& CurrentName.Equals(R.ExecutableName, ESearchCase::IgnoreCase);

		if (!bNameMatches)
		{
			UE_LOG(LogUnrealMCPChat, Verbose,
				TEXT("Leaving pid %u alone: expected '%s', found '%s' — the id was recycled."),
				R.ProcessId, *R.ExecutableName, *CurrentName);
			FPlatformProcess::CloseProc(Handle);
			++Skipped;
			continue;
		}

		UE_LOG(LogUnrealMCPChat, Warning,
			TEXT("Reaping orphaned agent '%s' (pid %u, %s) left over from a previous session."),
			*R.Label, R.ProcessId, *CurrentName);

		FPlatformProcess::TerminateProc(Handle, /*KillTree*/ true);
		FPlatformProcess::CloseProc(Handle);
		++Killed;
	}

	Records.Reset();
	Save();

	if (Killed > 0 || Skipped > 0)
	{
		UE_LOG(LogUnrealMCPChat, Log, TEXT("Agent sweep: %d reaped, %d left alone (recycled ids)."),
			Killed, Skipped);
	}
}

void FMCPChatPidRegistry::Record(uint32 ProcessId, const FString& Label, const FString& ExecutableName)
{
	if (ProcessId == 0) { return; }

	FScopeLock ScopeLock(&Lock);
	Load();

	FRecord R;
	R.ProcessId      = ProcessId;
	R.Label          = Label;
	R.ExecutableName = FPaths::GetCleanFilename(ExecutableName);
	R.SpawnedAtUnix  = FDateTime::UtcNow().ToUnixTimestamp();

	Records.RemoveAll([ProcessId](const FRecord& Existing) { return Existing.ProcessId == ProcessId; });
	Records.Add(MoveTemp(R));
	Save();
}

void FMCPChatPidRegistry::Forget(uint32 ProcessId)
{
	FScopeLock ScopeLock(&Lock);
	Load();

	if (Records.RemoveAll([ProcessId](const FRecord& R) { return R.ProcessId == ProcessId; }) > 0)
	{
		Save();
	}
}

void FMCPChatPidRegistry::KillAllTracked()
{
	FScopeLock ScopeLock(&Lock);
	Load();

	for (const FRecord& R : Records)
	{
		if (R.ProcessId == 0) { continue; }

		FProcHandle Handle = FPlatformProcess::OpenProcess(R.ProcessId);
		if (!Handle.IsValid()) { continue; }

		if (FPlatformProcess::IsProcRunning(Handle))
		{
			UE_LOG(LogUnrealMCPChat, Log, TEXT("Stopping agent '%s' (pid %u)."), *R.Label, R.ProcessId);
			FPlatformProcess::TerminateProc(Handle, /*KillTree*/ true);
		}
		FPlatformProcess::CloseProc(Handle);
	}

	Records.Reset();
	Save();
}
