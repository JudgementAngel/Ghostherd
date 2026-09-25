// Copyright StraySpark Studio 2026. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "MCPProtocol.h"
#include "Containers/Ticker.h"

/**
 * v4 Phase 1 — long-running tool execution as pollable tasks (MCP 2025-11-25
 * "Tasks" model, adapted to tools so every client can use it without
 * capability negotiation).
 *
 * Why polling, not SSE push: UE's FHttpServerModule completes each request
 * with a single FHttpServerResponse — there is no chunked/streaming response
 * support, so a server-push channel would mean vendoring an HTTP stack. The
 * 2025-11-25 spec's task-polling model is the sanctioned alternative.
 *
 * Flow:
 *  - Tools opt in with .LongRunning(). When called over HTTP, the registry
 *    starts the handler on the game thread WITHOUT blocking the network
 *    thread, waits a short grace period, and either returns the finished
 *    result (fast case — behaves exactly like v3) or a task handle:
 *      {task_id, status: "working", hint}
 *  - The agent polls get_task_status(task_id) for progress/result, or calls
 *    cancel_task(task_id) (cooperative).
 *  - Finished records are swept after a TTL so the map can't grow unbounded.
 */
class UNREALMCPSERVER_API FMCPTaskManager
{
public:
	enum class ETaskState : uint8
	{
		Working,
		Completed,
		Failed,
		Cancelled,
	};

	struct FTaskSnapshot
	{
		FString TaskId;
		FString ToolName;
		ETaskState State = ETaskState::Working;
		float Progress = 0.f;          // 0..1, -1 = unknown
		FString ProgressMessage;
		double CreatedAt = 0.0;
		double FinishedAt = 0.0;       // 0 while working
		FMCPToolResult Result;         // valid when Completed/Failed
	};

	static FMCPTaskManager& Get();

	/** Create a Working task record; returns the task id. */
	FString CreateTask(const FString& ToolName, const TSharedPtr<TAtomic<bool>>& CancelFlag, const FMCPRequestContext& Context);

	/** Mark finished. State derives from the result (and cancellation). */
	void CompleteTask(const FString& TaskId, const FMCPToolResult& Result, bool bWasCancelled);

	/** Progress sink target for the async run. */
	void UpdateProgress(const FString& TaskId, float Fraction, const FString& Message);

	/** Snapshot accessors (copies — safe across threads). */
	bool GetTask(const FString& TaskId, FTaskSnapshot& OutSnapshot, const FMCPRequestContext& Context) const;
	TArray<FTaskSnapshot> ListTasks(const FMCPRequestContext& Context) const;

	/** Flip the task's cancel flag (cooperative). False if unknown/finished. */
	bool CancelTask(const FString& TaskId, const FMCPRequestContext& Context);
	void CancelSessionTasks(const FString& SessionId);

	static FString TaskStateToString(ETaskState State);

private:
	FMCPTaskManager() = default;

	void EnsureSweepTicker();
	bool Sweep(float DeltaTime);

	struct FTaskRecord
	{
		FTaskSnapshot Snapshot;
		FString PrincipalId;
		FString SessionId;
		bool OwnedBy(const FMCPRequestContext& Context) const { return PrincipalId == Context.PrincipalId && SessionId == Context.SessionId; }
		TSharedPtr<TAtomic<bool>> CancelFlag;
	};

	TMap<FString, FTaskRecord> Tasks;
	mutable FCriticalSection TasksLock;
	FTSTicker::FDelegateHandle SweepTickerHandle;
};
