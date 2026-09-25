// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "MCPTaskManager.h"
#include "Misc/Guid.h"

namespace
{
	// Finished task records are kept this long for late polls, then swept.
	constexpr double FinishedTaskTTLSeconds = 3600.0;
}

FMCPTaskManager& FMCPTaskManager::Get()
{
	static FMCPTaskManager Instance;
	return Instance;
}

FString FMCPTaskManager::TaskStateToString(ETaskState State)
{
	switch (State)
	{
	case ETaskState::Working:   return TEXT("working");
	case ETaskState::Completed: return TEXT("completed");
	case ETaskState::Failed:    return TEXT("failed");
	case ETaskState::Cancelled: return TEXT("cancelled");
	}
	return TEXT("unknown");
}

FString FMCPTaskManager::CreateTask(const FString& ToolName, const TSharedPtr<TAtomic<bool>>& CancelFlag, const FMCPRequestContext& Context)
{
	FTaskRecord Record;
	Record.Snapshot.TaskId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
	Record.Snapshot.ToolName = ToolName;
	Record.Snapshot.State = ETaskState::Working;
	Record.Snapshot.Progress = -1.f;
	Record.Snapshot.CreatedAt = FPlatformTime::Seconds();
	Record.CancelFlag = CancelFlag;
	Record.PrincipalId = Context.PrincipalId;
	Record.SessionId = Context.SessionId;

	FString TaskId = Record.Snapshot.TaskId;
	{
		FScopeLock Lock(&TasksLock);
		Tasks.Add(TaskId, MoveTemp(Record));
	}
	EnsureSweepTicker();
	UE_LOG(LogUnrealMCP, Log, TEXT("Task created: %s (%s)"), *TaskId, *ToolName);
	return TaskId;
}

void FMCPTaskManager::CompleteTask(const FString& TaskId, const FMCPToolResult& Result, bool bWasCancelled)
{
	FScopeLock Lock(&TasksLock);
	if (FTaskRecord* Record = Tasks.Find(TaskId))
	{
		Record->Snapshot.Result = Result;
		Record->Snapshot.FinishedAt = FPlatformTime::Seconds();
		Record->Snapshot.State = bWasCancelled ? ETaskState::Cancelled
			: (Result.bIsError ? ETaskState::Failed : ETaskState::Completed);
		UE_LOG(LogUnrealMCP, Log, TEXT("Task %s: %s"), *TaskId, *TaskStateToString(Record->Snapshot.State));
	}
}

void FMCPTaskManager::UpdateProgress(const FString& TaskId, float Fraction, const FString& Message)
{
	FScopeLock Lock(&TasksLock);
	if (FTaskRecord* Record = Tasks.Find(TaskId))
	{
		Record->Snapshot.Progress = FMath::Clamp(Fraction, 0.f, 1.f);
		Record->Snapshot.ProgressMessage = Message;
	}
}

bool FMCPTaskManager::GetTask(const FString& TaskId, FTaskSnapshot& OutSnapshot, const FMCPRequestContext& Context) const
{
	FScopeLock Lock(&TasksLock);
	if (const FTaskRecord* Record = Tasks.Find(TaskId))
	{
		if (!Record->OwnedBy(Context)) return false;
		OutSnapshot = Record->Snapshot;
		return true;
	}
	return false;
}

TArray<FMCPTaskManager::FTaskSnapshot> FMCPTaskManager::ListTasks(const FMCPRequestContext& Context) const
{
	FScopeLock Lock(&TasksLock);
	TArray<FTaskSnapshot> Result;
	Result.Reserve(Tasks.Num());
	for (const auto& Pair : Tasks)
	{
		if (Pair.Value.OwnedBy(Context)) Result.Add(Pair.Value.Snapshot);
	}
	Result.Sort([](const FTaskSnapshot& A, const FTaskSnapshot& B) { return A.CreatedAt > B.CreatedAt; });
	return Result;
}

bool FMCPTaskManager::CancelTask(const FString& TaskId, const FMCPRequestContext& Context)
{
	FScopeLock Lock(&TasksLock);
	if (FTaskRecord* Record = Tasks.Find(TaskId))
	{
		if (Record->OwnedBy(Context) && Record->Snapshot.State == ETaskState::Working && Record->CancelFlag.IsValid())
		{
			Record->CancelFlag->Store(true, EMemoryOrder::Relaxed);
			UE_LOG(LogUnrealMCP, Log, TEXT("Task %s: cancellation requested"), *TaskId);
			return true;
		}
	}
	return false;
}

void FMCPTaskManager::EnsureSweepTicker()
{
	if (!SweepTickerHandle.IsValid())
	{
		SweepTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateRaw(this, &FMCPTaskManager::Sweep), 300.0f);
	}
}

bool FMCPTaskManager::Sweep(float /*DeltaTime*/)
{
	const double Now = FPlatformTime::Seconds();
	FScopeLock Lock(&TasksLock);
	for (auto It = Tasks.CreateIterator(); It; ++It)
	{
		const FTaskSnapshot& Snap = It->Value.Snapshot;
		if (Snap.State != ETaskState::Working && Now - Snap.FinishedAt > FinishedTaskTTLSeconds)
		{
			It.RemoveCurrent();
		}
	}
	return true;
}

void FMCPTaskManager::CancelSessionTasks(const FString& SessionId)
{
    if (SessionId.IsEmpty()) return;
    FScopeLock Lock(&TasksLock);
    for (auto& Pair : Tasks)
        if (Pair.Value.SessionId == SessionId && Pair.Value.Snapshot.State == ETaskState::Working && Pair.Value.CancelFlag.IsValid())
            Pair.Value.CancelFlag->Store(true);
}
