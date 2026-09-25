// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "MCPToolRegistry.h"
#include "MCPSettings.h"
#include "MCPTaskManager.h"
#include "MCPInputValidator.h"
#include "MCPTransactionScope.h"
#include "MCPDiagnostics.h"
#include "Editor.h"
#include "ScopedTransaction.h"
#include "Async/Async.h"
#include "Async/Future.h"

#if !PLATFORM_EXCEPTIONS_DISABLED
#include <exception>
#endif

namespace
{
    thread_local const FMCPRequestContext* ActiveToolContext = nullptr;
    thread_local int32 ActiveToolDepth = 0;
    thread_local bool ActivePreview = false;
}

FMCPToolRegistry& FMCPToolRegistry::Get()
{
	static FMCPToolRegistry Instance;
	return Instance;
}

void FMCPToolRegistry::RegisterTool(const FMCPToolDefinition& Tool)
{
	FScopeLock Lock(&ToolsLock);

	if (Tools.Contains(Tool.Name))
	{
		UE_LOG(LogUnrealMCP, Warning, TEXT("Tool '%s' already registered, overwriting"), *Tool.Name);
	}

	Tools.Add(Tool.Name, Tool);
	bListCacheDirty = true;
	UE_LOG(LogUnrealMCP, Log, TEXT("Registered tool: %s"), *Tool.Name);
}

void FMCPToolRegistry::UnregisterTool(const FString& Name)
{
	FScopeLock Lock(&ToolsLock);
	Tools.Remove(Name);
	bListCacheDirty = true;
	UE_LOG(LogUnrealMCP, Log, TEXT("Unregistered tool: %s"), *Name);
}

void FMCPToolRegistry::UnregisterAllTools()
{
	FScopeLock Lock(&ToolsLock);
	Tools.Empty();
	bListCacheDirty = true;
}

TMap<FName, TArray<FString>> FMCPToolRegistry::GetToolsByCategory() const
{
	FScopeLock Lock(&ToolsLock);
	TMap<FName, TArray<FString>> Result;
	for (const auto& Pair : Tools)
	{
		Result.FindOrAdd(Pair.Value.Category.IsNone() ? FName(TEXT("Uncategorized")) : Pair.Value.Category)
			.Add(Pair.Key);
	}
	for (auto& Pair : Result)
	{
		Pair.Value.Sort();
	}
	return Result;
}

TArray<TSharedPtr<FJsonValue>> FMCPToolRegistry::GetToolsListJson(bool bFullSchemas) const
{
	FScopeLock Lock(&ToolsLock);
	if (bListCacheDirty)
	{
		CachedListSlim.Empty(Tools.Num());
		CachedListFull.Empty(Tools.Num());
		// Stable ordering so cursor pagination stays consistent between calls.
		TArray<FString> Names;
		Tools.GenerateKeyArray(Names);
		Names.Sort();
		for (const FString& Name : Names)
		{
			const FMCPToolDefinition& Tool = Tools[Name];
			CachedListSlim.Add(MakeShared<FJsonValueObject>(Tool.ToJsonSlim()));
			CachedListFull.Add(MakeShared<FJsonValueObject>(Tool.ToJson()));
		}
		bListCacheDirty = false;
	}
	return bFullSchemas ? CachedListFull : CachedListSlim;
}

// ---------------------------------------------------------------------------
// Phase 0 / R3 — catalog-mode exposure, shared by every front-end.
// ---------------------------------------------------------------------------

const TSet<FString>& FMCPToolRegistry::GetCatalogCoreToolNames()
{
	static const TSet<FString> CatalogCoreTools = {
		// meta / discovery
		TEXT("search_tools"), TEXT("get_tool_schemas"), TEXT("list_tool_categories"), TEXT("run_tool_script"),
		TEXT("search_project"), TEXT("rebuild_search_index"),
		// background tasks (v4) — polling must work without discovery
		TEXT("get_task_status"), TEXT("cancel_task"), TEXT("list_tasks"),
		// scene basics
		TEXT("list_actors"), TEXT("find_actors"), TEXT("create_actor"), TEXT("destroy_actors"),
		TEXT("set_actor_transform"), TEXT("set_actor_property"), TEXT("get_actor_properties"),
		TEXT("get_spatial_context"), TEXT("place_actor_on_ground"),
		// assets & level
		TEXT("list_assets"), TEXT("get_asset_info"), TEXT("get_level_info"), TEXT("save_level"), TEXT("open_level"),
		// visual feedback & project
		TEXT("take_screenshot"), TEXT("set_viewport_camera"), TEXT("get_project_info"),
		// scripting entry points
		TEXT("create_blueprint"), TEXT("get_blueprint_info"), TEXT("compile_blueprint"),
		TEXT("run_console_command"), TEXT("execute_python"),
		// v4.6 animation read-back + compile. These three are the verify half of
		// animation authoring: an agent cannot correct a montage or an AnimGraph it
		// cannot read, and compile_anim_blueprint is the only signal that a state
		// machine is actually valid. Discovering them via search_tools first would
		// cost a round trip on every animation edit loop.
		TEXT("montage_get_sections"), TEXT("animgraph_describe"), TEXT("compile_anim_blueprint"),
	};
	return CatalogCoreTools;
}

TArray<TSharedPtr<FJsonValue>> FMCPToolRegistry::GetToolsForExposure(bool bCatalogMode, bool bFullSchemas) const
{
	TArray<TSharedPtr<FJsonValue>> All = GetToolsListJson(bFullSchemas);
	if (!bCatalogMode)
	{
		return All;
	}

	const TSet<FString>& Core = GetCatalogCoreToolNames();
	TArray<TSharedPtr<FJsonValue>> Selected;
	Selected.Reserve(Core.Num());
	for (const TSharedPtr<FJsonValue>& ToolVal : All)
	{
		const TSharedPtr<FJsonObject>* ToolObj = nullptr;
		if (ToolVal->TryGetObject(ToolObj) && Core.Contains((*ToolObj)->GetStringField(TEXT("name"))))
		{
			Selected.Add(ToolVal);
		}
	}
	return Selected;
}

TArray<FMCPToolDefinition> FMCPToolRegistry::GetToolDefinitionsForExposure(bool bCatalogMode) const
{
	FScopeLock Lock(&ToolsLock);

	TArray<FString> Names;
	Tools.GenerateKeyArray(Names);
	Names.Sort();   // stable ordering keeps provider-side prompt caching warm

	const TSet<FString>& Core = GetCatalogCoreToolNames();
	TArray<FMCPToolDefinition> Result;
	Result.Reserve(bCatalogMode ? Core.Num() : Names.Num());
	for (const FString& Name : Names)
	{
		if (!bCatalogMode || Core.Contains(Name))
		{
			Result.Add(Tools[Name]);
		}
	}
	return Result;
}

const FMCPToolDefinition* FMCPToolRegistry::FindTool(const FString& Name) const
{
	FScopeLock Lock(&ToolsLock);
	return Tools.Find(Name);
}

TArray<FMCPToolDefinition> FMCPToolRegistry::GetAllTools() const
{
	FScopeLock Lock(&ToolsLock);
	TArray<FMCPToolDefinition> Result;
	Tools.GenerateValueArray(Result);
	return Result;
}

int32 FMCPToolRegistry::GetToolCount() const
{
	FScopeLock Lock(&ToolsLock);
	return Tools.Num();
}

FMCPToolResult FMCPToolRegistry::ExecuteTool(const FString& Name, const TSharedPtr<FJsonObject>& Arguments)
{
    // Synchronous nested calls inherit the active caller, including legacy handlers.
    if (ActiveToolContext) return ExecuteTool(Name, Arguments, *ActiveToolContext);

	FMCPRequestContext InternalContext;
	InternalContext.Scope = EMCPScope::Destructive;
	return ExecuteTool(Name, Arguments, InternalContext);
}

FMCPToolResult FMCPToolRegistry::ExecuteTool(const FString& Name, const TSharedPtr<FJsonObject>& Arguments,
	const FMCPRequestContext& InContext)
{
    FMCPRequestContext Context = InContext;
    if (ActiveToolContext)
    {
        Context = *ActiveToolContext;
        if (static_cast<uint8>(InContext.Scope) < static_cast<uint8>(Context.Scope)) Context.Scope = InContext.Scope;
        Context.bAllowAsyncTask = false;
    }
    if (ActiveToolDepth >= 32)
        return FMCPToolResult::ErrorStructured(EMCPError::Unsupported, TEXT("Nested tool call depth exceeded"));
    if (Context.IsCancelled()) return FMCPToolResult::Error(TEXT("Request cancelled before tool dispatch"));

    // Own the definition for the entire dispatch; provider refresh cannot invalidate it.
    FMCPToolDefinition Snapshot;
    bool bFound = false;
    {
        FScopeLock Lock(&ToolsLock);
        if (const auto* Found = Tools.Find(Name)) { Snapshot = *Found; bFound = true; }
    }
    const FMCPToolDefinition* Tool = bFound ? &Snapshot : nullptr;
	if (!Tool)
	{
		// Did-you-mean for the tool name itself.
		TArray<FString> Suggestions;
		{
			FScopeLock Lock(&ToolsLock);
			for (const auto& Pair : Tools)
			{
				if (Pair.Key.Contains(Name) || Name.Contains(Pair.Key)) { Suggestions.Add(Pair.Key); }
				if (Suggestions.Num() >= 3) break;
			}
		}
		return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
			FString::Printf(TEXT("Unknown tool: %s"), *Name),
			TEXT("Use tools/list to enumerate available tools."),
			Suggestions);
	}

	if (!Tool->Handler.IsBound() && !Tool->HandlerCtx.IsBound())
	{
		return FMCPToolResult::ErrorStructured(EMCPError::Internal,
			FString::Printf(TEXT("Tool '%s' has no handler"), *Name));
	}

    const auto Validation = FMCPInputValidator::Validate(Tool->InputSchema, Arguments);
    if (!Validation.bOk) return Validation.Error;
    if (ActivePreview && !Tool->bReadOnlyHint)
        return FMCPToolResult::ErrorStructured(EMCPError::Unsupported, TEXT("Preview cannot dispatch a mutating child tool"));

	// Phase C / C6: refuse tools annotated bRequiresPieOff while PIE is active.
	if (Tool->bRequiresPieOff && GEditor && GEditor->IsPlaySessionInProgress())
	{
		return FMCPToolResult::ErrorStructured(EMCPError::RequiresPieOff,
			FString::Printf(TEXT("Tool '%s' cannot run while PIE is active."), *Name),
			TEXT("Stop Play-In-Editor (Esc / Stop button) and retry."));
	}

	// Scope gate: refuse destructive tools when caller's scope is below Destructive.
	if (Tool->bDestructiveHint && !Context.HasScope(EMCPScope::Destructive))
	{
		return FMCPToolResult::ErrorStructured(EMCPError::ScopeDenied,
			FString::Printf(TEXT("Tool '%s' is destructive and requires Destructive scope."), *Name),
			TEXT("Provide a token mapped to Destructive scope, or set bAllowDestructiveScope=true with auth disabled."));
	}

	// Scope gate: refuse mutating tools when the caller's scope is below Scene.
	// This is what makes a "read" token an actual read-only session — before it
	// existed, Read differed from Scene only for destructive tools, so a token
	// mapped to "read" could still call set_actor_property, the Blueprint
	// authoring family, or execute_python. It also backs the Chat module's
	// ReadOnly approval mode, which maps to EMCPScope::Read.
	//
	// Fail-closed: a tool counts as mutating unless it is annotated .ReadOnly().
	// An unannotated tool is therefore denied to a Read session rather than
	// waved through on the assumption that its handler is side-effect free.
	if (!Tool->bReadOnlyHint && !Context.HasScope(EMCPScope::Scene))
	{
		return FMCPToolResult::ErrorStructured(EMCPError::ScopeDenied,
			FString::Printf(TEXT("Tool '%s' mutates editor state; this session is Read-only."), *Name),
			TEXT("Map this token to 'scene' (or 'destructive') in AuthTokenScopes, or remove its entry to inherit the global default scope."));
	}

	// A malformed preview flag must never fall through to a real edit.
	if (Arguments.IsValid() && Arguments->HasField(TEXT("dry_run"))
		&& !Arguments->HasTypedField<EJson::Boolean>(TEXT("dry_run")))
	{
		return FMCPToolResult::ErrorStructured(EMCPError::OutOfRange,
			TEXT("'dry_run' must be a boolean."));
	}

	// Preview requires a dedicated preflight implementation.
	const bool bDryRun = Arguments.IsValid() && Arguments->HasTypedField<EJson::Boolean>(TEXT("dry_run"))
		&& Arguments->GetBoolField(TEXT("dry_run"));
	if (bDryRun && !Tool->SupportsSafePreview())
	{
		return FMCPToolResult::ErrorStructured(EMCPError::Unsupported,
			FString::Printf(TEXT("Tool '%s' does not support dry_run."), *Name),
			TEXT("Re-run without dry_run, or check tools/list annotations.supportsDryRun before requesting it."));
	}

	// Execute on game thread to safely access UE APIs.
	// v4 Phase 0 hardening (replaces v3's unbounded FEvent wait):
	//  - the handler runs inside an exception guard so a throwing tool returns a
	//    structured error instead of crashing the editor
	//  - off-thread callers wait with a configurable timeout instead of blocking a
	//    network thread forever behind a stalled game thread (modal dialog, long GC)
	//  - the deferred task owns value-copies of everything it touches, so a timed-out
	//    request can never leave the game thread writing into dead stack memory
	FMCPToolHandler HandlerCopy = Tool->Handler;
	FMCPToolHandlerCtx HandlerCtxCopy = bDryRun ? Tool->PreviewHandlerCtx : Tool->HandlerCtx;   // v4 Phase 1: preferred when bound
	const FString ToolName = Tool->Name;
	const bool bReadOnly = Tool->bReadOnlyHint;

	// v4: long-running tools called over HTTP become pollable tasks. Decide the
	// path BEFORE building the execution lambda so the task's progress sink and
	// cancel flag can be wired into the context the handler will see.
	const bool bAsTask = Tool->bLongRunningHint && Context.bAllowAsyncTask && !IsInGameThread() && !bDryRun;
	FMCPRequestContext ExecContext = Context;
	FString TaskId;
	TSharedPtr<TAtomic<bool>> TaskCancelFlag;
	if (bAsTask)
	{
		TaskCancelFlag = ExecContext.CancelFlag.IsValid() ? ExecContext.CancelFlag : MakeShared<TAtomic<bool>>(false);
		ExecContext.CancelFlag = TaskCancelFlag;
		TaskId = FMCPTaskManager::Get().CreateTask(ToolName, TaskCancelFlag, ExecContext);
		const FString TaskIdForSink = TaskId;
		// v4.5 — chain (don't replace) any caller-provided sink (e.g. the HTTP
		// layer's SSE push), so progress reaches BOTH get_task_status and the
		// client's live SSE stream.
		TFunction<void(float, const FString&)> PriorSink = ExecContext.ProgressSink;
		ExecContext.ProgressSink = [TaskIdForSink, PriorSink](float Fraction, const FString& Message)
		{
			FMCPTaskManager::Get().UpdateProgress(TaskIdForSink, Fraction, Message);
			if (PriorSink) { PriorSink(Fraction, Message); }
		};
	}
	const FMCPRequestContext ContextCopy = ExecContext;

	auto RunGuarded = [this, HandlerCopy, HandlerCtxCopy, Arguments, bDryRun, ToolName, ContextCopy, bReadOnly]() -> FMCPToolResult
	{
		if (!bReadOnly && !bDryRun)
        {
            const auto Owners = FMCPTransactionManager::Get().GetOpenOwners();
            if ((!Owners.IsEmpty() && !Owners.Contains(ContextCopy.SessionId))
                || (Owners.IsEmpty() && !ActiveToolContext && GEditor && GEditor->IsTransactionActive())
                || (ActiveToolContext && (ActiveToolContext->PrincipalId != ContextCopy.PrincipalId || ActiveToolContext->SessionId != ContextCopy.SessionId)))
                return FMCPToolResult::ErrorStructured(EMCPError::Unsupported, TEXT("Editor transaction is owned by another operation; retry after it finishes"));
        }
        auto CallHandler = [&]() -> FMCPToolResult
		{
            if (ContextCopy.IsCancelled()) return FMCPToolResult::Error(TEXT("Request cancelled before handler execution"));
            TGuardValue<const FMCPRequestContext*> CallerGuard(ActiveToolContext, &ContextCopy);
            TGuardValue<int32> DepthGuard(ActiveToolDepth, ActiveToolDepth + 1);
            TGuardValue<bool> PreviewGuard(ActivePreview, ActivePreview || bDryRun);
			return HandlerCtxCopy.IsBound()
				? HandlerCtxCopy.Execute(Arguments, ContextCopy)
				: HandlerCopy.Execute(Arguments);
		};

		auto Invoke = [&]() -> FMCPToolResult
		{
			if (bDryRun)
			{
				// Preview invokes only a dedicated non-mutating handler. Cancelling an
				// Unreal transaction discards undo history; it does not undo edits.
				return CallHandler();
			}
			if (!bReadOnly)
			{
				// v5: never open the registry undo step while a PIE session is running.
				// A transaction recorded during PIE captures PIE-world objects (widgets,
				// GameInstance) in the transaction buffer; ending PIE then asserts in
				// PlayLevel.cpp because the old GameInstance is still referenced
				// (reproduced 2026-09-09 with pie_add_widget_to_viewport + pie_stop).
				// Edits made during PIE therefore record no undo step; results say so.
				if (GEditor && GEditor->IsPlaySessionInProgress())
				{
					FMCPToolResult NoUndo = CallHandler();
					if (NoUndo.StructuredContent.IsValid()) NoUndo.StructuredContent->SetBoolField(TEXT("undo_recorded"), false);
					return NoUndo;
				}
				// v4 Phase 2 — universal transactional contract: every mutating tool
				// gets a named undo step at the registry level (v3 had exactly ONE tool
				// doing this). Tools' own transactions nest into this one; non-undoable
				// side effects (file saves, external APIs) are unaffected.
				FScopedTransaction Txn(FText::FromString(FString::Printf(TEXT("MCP: %s"), *ToolName)));
				return CallHandler();
			}
			return CallHandler();
		};

		// v4.5 Phase 0 / R4 — collect the result rather than returning straight out of
		// the guard, so a single broadcast point covers the success, std::exception and
		// unknown-exception paths alike.
		FMCPToolResult Outcome;
		const double DispatchStart = FPlatformTime::Seconds(); // v5 increment 20: timing telemetry

#if PLATFORM_EXCEPTIONS_DISABLED
		Outcome = Invoke();
#else
		try
		{
			Outcome = Invoke();
		}
		catch (const std::exception& Ex)
		{
			UE_LOG(LogUnrealMCP, Error, TEXT("Tool '%s' threw std::exception: %hs"), *ToolName, Ex.what());
			Outcome = FMCPToolResult::ErrorStructured(EMCPError::Internal,
				FString::Printf(TEXT("Tool '%s' threw an exception: %hs"), *ToolName, Ex.what()),
				TEXT("This is a plugin bug — please report it. The editor was protected from crashing."));
		}
		catch (...)
		{
			UE_LOG(LogUnrealMCP, Error, TEXT("Tool '%s' threw an unknown exception"), *ToolName);
			Outcome = FMCPToolResult::ErrorStructured(EMCPError::Internal,
				FString::Printf(TEXT("Tool '%s' threw an unknown exception."), *ToolName),
				TEXT("This is a plugin bug — please report it. The editor was protected from crashing."));
		}
#endif

		MCPDiagnostics::RecordToolCall(ToolName, (FPlatformTime::Seconds() - DispatchStart) * 1000.0, !Outcome.bIsError);

		// RunGuarded only ever executes on the game thread (either the direct
		// IsInGameThread() path below, or inside AsyncTask(ENamedThreads::GameThread)),
		// so observers are guaranteed a game-thread callback and can touch Slate.
		// Guarded so a throwing observer can't turn a successful tool call into a crash.
		if (OnToolExecuted.IsBound())
		{
#if PLATFORM_EXCEPTIONS_DISABLED
			OnToolExecuted.Broadcast(ToolName, ContextCopy, Outcome);
#else
			try
			{
				OnToolExecuted.Broadcast(ToolName, ContextCopy, Outcome);
			}
			catch (...)
			{
				UE_LOG(LogUnrealMCP, Error,
					TEXT("An OnToolExecuted observer threw while handling '%s'; ignoring."), *ToolName);
			}
#endif
		}

		return Outcome;
	};

	if (IsInGameThread())
	{
		return RunGuarded();
	}

	// v4 — pollable-task path for .LongRunning() tools: never block the network
	// thread for the full duration. Run on the game thread, give it a short
	// grace window (fast runs behave exactly like normal calls), and on overrun
	// hand back a task id the agent can poll with get_task_status.
	if (bAsTask)
	{
		TSharedRef<TPromise<FMCPToolResult>, ESPMode::ThreadSafe> TaskPromise =
			MakeShared<TPromise<FMCPToolResult>, ESPMode::ThreadSafe>();
		TFuture<FMCPToolResult> TaskFuture = TaskPromise->GetFuture();

		const FString TaskIdCopy = TaskId;
		TSharedPtr<TAtomic<bool>> FlagCopy = TaskCancelFlag;
		AsyncTask(ENamedThreads::GameThread, [TaskPromise, RunGuarded, TaskIdCopy, FlagCopy]()
		{
			FMCPToolResult TaskResult = RunGuarded();
			FMCPTaskManager::Get().CompleteTask(TaskIdCopy, TaskResult,
				FlagCopy.IsValid() && FlagCopy->Load(EMemoryOrder::Relaxed));
			TaskPromise->SetValue(MoveTemp(TaskResult));
		});

		constexpr double GraceSeconds = 3.0;
		if (TaskFuture.WaitFor(FTimespan::FromSeconds(GraceSeconds)))
		{
			return TaskFuture.Get(); // finished within the grace window — normal result
		}

		TSharedPtr<FJsonObject> TaskObj = MakeShared<FJsonObject>();
		TaskObj->SetStringField(TEXT("task_id"), TaskIdCopy);
		TaskObj->SetStringField(TEXT("status"), TEXT("working"));
		TaskObj->SetStringField(TEXT("tool"), ToolName);
		return FMCPToolResult::SuccessStructured(
			FString::Printf(TEXT("'%s' is still running and was converted to a background task (task_id: %s). Poll get_task_status for progress and the final result; cancel_task to abort."),
				*ToolName, *TaskIdCopy),
			TaskObj);
	}

	const UMCPSettings* Settings = UMCPSettings::Get();
	const int32 TimeoutSeconds = Settings ? Settings->ToolCallTimeoutSeconds : 60;

	TSharedRef<TPromise<FMCPToolResult>, ESPMode::ThreadSafe> Promise =
		MakeShared<TPromise<FMCPToolResult>, ESPMode::ThreadSafe>();
	TFuture<FMCPToolResult> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [Promise, RunGuarded]()
	{
		Promise->SetValue(RunGuarded());
	});

	if (TimeoutSeconds <= 0)
	{
		return Future.Get(); // 0 = wait forever (v3 behavior)
	}

	if (Future.WaitFor(FTimespan::FromSeconds((double)TimeoutSeconds)))
	{
		return Future.Get();
	}

	UE_LOG(LogUnrealMCP, Error, TEXT("Tool '%s' timed out after %ds waiting on the game thread"), *ToolName, TimeoutSeconds);
	return FMCPToolResult::ErrorStructured(EMCPError::Timeout,
		FString::Printf(TEXT("Tool '%s' did not complete within %d seconds."), *ToolName, TimeoutSeconds),
		TEXT("The editor may be blocked by a modal dialog or a long operation. The tool may still finish in the editor — verify its effect before retrying. Raise ToolCallTimeoutSeconds in Project Settings if this tool is legitimately slow."));
}
