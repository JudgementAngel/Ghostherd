// Copyright StraySpark Studio 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IMCPChatBackend.h"
#include "MCPChatAgentCatalog.h"
#include "Containers/Ticker.h"

class FMCPChatProcessRunner;
class FMCPJsonRpcStdio;

/**
 * Phase 7 — a local CLI agent, driven over the Agent Client Protocol.
 *
 * One class covers Claude Code, Codex, Gemini CLI, Kimi, OpenCode, iFlow and
 * anything else that speaks ACP, because the differences between them are data
 * (Config/DefaultChatAgents.json), not code. That is the whole reason ACP was
 * chosen over five bespoke adapters (docs/01 §4.3).
 *
 * ── The inversion worth understanding ──────────────────────────────────────
 * A MODEL backend owns the tool loop: we execute tools ourselves through
 * FMCPChatToolBridge, in-process, under our approval gate.
 *
 * An AGENT backend does not. The agent runs its OWN loop and reaches the editor
 * over the MCP HTTP endpoint we hand it at spawn. Its tool calls arrive here as
 * `session/update` notifications that we RENDER; we do not execute them.
 *
 * The consequence is a real seam and it is documented rather than papered over:
 * an agent's editor access is governed by the MCP *server's* scope setting and by
 * whatever it chooses to ask permission for — not by the chat panel's approval
 * mode. Where the agent does ask (`session/request_permission`), we apply the
 * panel's policy on top and can auto-deny; where it does not, the server's scope
 * is the control. Both are surfaced in the UI so nobody has to infer it.
 */
class FAcpAgentBackend : public IMCPChatBackend, public TSharedFromThis<FAcpAgentBackend>
{
public:
	explicit FAcpAgentBackend(const FMCPChatAgentInfo& InAgent);
	virtual ~FAcpAgentBackend() override;

	//~ IMCPChatBackend
	virtual EChatBackendKind GetKind() const override { return EChatBackendKind::Agent; }
	virtual FString GetId() const override { return Agent.AgentId; }
	virtual FText   GetDisplayName() const override { return FText::FromString(Agent.DisplayName); }
	virtual bool    IsAvailable(FText& OutReason) const override;
	virtual TArray<FChatModelInfo> GetModels() const override;

	virtual void StartSession(const FGuid& SessionId) override;
	virtual void SendTurn(const FChatTurnRequest& Request) override;
	virtual void CancelTurn(const FGuid& SessionId) override;
	virtual void EndSession(const FGuid& SessionId) override;
	virtual void RespondToPermission(const FGuid& RequestId, EChatPermissionResult Result) override;
	//~ End IMCPChatBackend

	/** Kill the child. Called from module shutdown as well as EndSession. */
	void Shutdown();

private:
	// ---- Lifecycle ----
	bool EnsureProcess(const FString& ModelId, FText& OutError);
	void BeginInitialize();
	void HandleInitializeResult(const TSharedPtr<FJsonObject>& Result, const TSharedPtr<FJsonObject>& Error);
	void EnsureAcpSession();
	void SendPendingPrompt();

	// ---- Inbound ----
	void HandleNotification(const FString& Method, const TSharedPtr<FJsonObject>& Params);
	void HandleRequest(const FString& Method, const TSharedPtr<FJsonObject>& Params,
	                   const TSharedPtr<FJsonValue>& Id);
	void HandleDiagnostic(const FString& Line);
	void HandleSessionUpdate(const TSharedPtr<FJsonObject>& Params);
	void HandleProcessExited(int32 ReturnCode, bool bWasRequested);

	// ---- Client-side methods the agent may call ----
	void HandleFsRead(const TSharedPtr<FJsonObject>& Params, const TSharedPtr<FJsonValue>& Id);
	void HandleFsWrite(const TSharedPtr<FJsonObject>& Params, const TSharedPtr<FJsonValue>& Id);
	void HandlePermissionRequest(const TSharedPtr<FJsonObject>& Params, const TSharedPtr<FJsonValue>& Id);
	void HandleTerminalCreate(const TSharedPtr<FJsonObject>& Params, const TSharedPtr<FJsonValue>& Id);
	void HandleTerminalOutput(const TSharedPtr<FJsonObject>& Params, const TSharedPtr<FJsonValue>& Id);
	void HandleTerminalWait(const TSharedPtr<FJsonObject>& Params, const TSharedPtr<FJsonValue>& Id);
	void HandleTerminalKill(const TSharedPtr<FJsonObject>& Params, const TSharedPtr<FJsonValue>& Id);
	void HandleTerminalRelease(const TSharedPtr<FJsonObject>& Params, const TSharedPtr<FJsonValue>& Id);

	/**
	 * Containment check for every `fs/*` path.
	 *
	 * The agent is a program the user installed, not a program we wrote, and it is
	 * being handed a filesystem API. Paths are resolved to absolute form FIRST and
	 * then tested for containment in the project directory — a prefix test on the
	 * raw string is defeated by `../../..`, which is the entire attack.
	 */
	bool IsPathAllowed(const FString& InPath, FString& OutAbsolute, FString& OutReason) const;

	// ---- Emission ----
	void Emit(const FChatStreamEvent& Event);
	void EmitText(const FString& Text);
	void EmitError(const FString& Message);
	/** Localised overload. Everything the user reads should come through here; the
	 *  FString form remains for provider-supplied text, which is not ours to
	 *  translate and must be shown verbatim. */
	void EmitError(const FText& Message);
	void FinishTurn();

	bool Tick(float DeltaTime);

	FMCPChatAgentInfo Agent;

	TSharedPtr<FMCPChatProcessRunner> Runner;
	TSharedPtr<FMCPJsonRpcStdio>      Rpc;

	/** ACP session id, as a string, from `session/new`. */
	FString AcpSessionId;

	/** The model the RUNNING process was launched with. A model change reaches an
	 *  agent only at spawn, so this is what tells us a restart is needed. */
	FString LaunchedModelId;

	FGuid ActiveSessionId;
	FGuid ActiveMessageId;
	TSharedPtr<TAtomic<bool>> CancelFlag;

	/** Set between SendTurn and the prompt actually going out — the process may
	 *  still be starting up, and the user's message must not be lost in the gap. */
	TSharedPtr<FJsonObject> PendingPromptParams;
	bool bTurnActive = false;

	enum class EState : uint8
	{
		Idle,
		Starting,
		Initializing,
		CreatingSession,
		Ready,
		Failed,
	};
	EState State = EState::Idle;

	/** Declared by the agent at initialize. Recorded for the resume path; a session
	 *  that cannot be loaded has to start fresh rather than fail. */
	bool bAgentSupportsLoadSession = false;

	/** Sign-in methods the agent ADVERTISES. Not evidence of being signed out —
	 *  only used to make a genuine session failure more actionable. */
	TArray<FString> AgentAuthMethods;

	/** The agent's string tool-call ids → our block GUIDs, so `tool_call_update`
	 *  finds the card that `tool_call` created. */
	TMap<FString, FGuid> ToolCallIds;

	/** Permission requests waiting on the user: our local id → the JSON-RPC id and
	 *  the option ids the agent offered. */
	struct FPendingPermission
	{
		TSharedPtr<FJsonValue> RpcId;
		FString AllowOptionId;
		FString AllowAlwaysOptionId;
		FString RejectOptionId;
		FString ToolName;
	};
	TMap<FGuid, FPendingPermission> PendingPermissions;

	/** Terminals the agent asked us to run, keyed by the handle we gave it. */
	struct FAgentTerminal
	{
		TSharedPtr<FMCPChatProcessRunner> Process;
		FString Output;
		int32   ExitCode = 0;
		bool    bExited = false;
		/** JSON-RPC ids blocked in terminal/wait_for_exit. */
		TArray<TSharedPtr<FJsonValue>> Waiters;
	};
	TMap<FString, TSharedPtr<FAgentTerminal>> Terminals;
	int32 NextTerminalId = 1;

	/** Availability probe result, cached — ResolveExecutable walks PATH and the
	 *  picker asks once per row per repaint. */
	mutable FString CachedResolvedPath;
	mutable double  CachedResolveTime = 0.0;
	static constexpr double AvailabilityCacheSeconds = 30.0;

	FTSTicker::FDelegateHandle TickerHandle;

	/** Wall-clock ceiling for a single agent turn. Agents can legitimately run for
	 *  minutes, so this is generous — it exists to catch a wedged process, not to
	 *  discipline a slow one. */
	static constexpr double MaxTurnSeconds = 1800.0;
	double TurnStartSeconds = 0.0;
};
