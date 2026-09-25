// Copyright StraySpark Studio 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Phase 7 — local agent presets, loaded from Config/DefaultChatAgents.json.
 *
 * Same reasoning as the model catalogue: agent CLIs change their flags faster than
 * a plugin ships. A hardcoded `--experimental-acp` is wrong the day it moves, and
 * the failure mode is an agent that will not start with no way for the user to fix
 * it. Here it is a one-line JSON edit, and the "custom agent" row means an agent
 * nobody has heard of yet works without a plugin update.
 */

/** How this agent is told about our MCP endpoint. */
enum class EMCPAgentMcpInjection : uint8
{
	/** Write mcp.json and pass it with a flag: `--mcp-config <path>`. */
	Flag,
	/** Write mcp.json and point an environment variable at it. */
	Environment,
	/** The agent has its own registration command; we do nothing and say so. */
	Manual,
	/** Agent does not consume MCP. */
	None,
};

struct UNREALMCPCHAT_API FMCPChatAgentInfo
{
	FString AgentId;
	FString DisplayName;

	/** Command name (resolved on PATH) or an absolute path. */
	FString Command;
	/** Arguments, already split — quoting rules differ per platform and getting
	 *  them wrong silently passes one giant argument. */
	TArray<FString> Arguments;

	TMap<FString, FString> Environment;

	EMCPAgentMcpInjection McpInjection = EMCPAgentMcpInjection::Flag;
	/** Flag form: the flag itself. Environment form: the variable name. */
	FString McpConfigFlag;

	/**
	 * Models this agent can be pointed at.
	 *
	 * ACP does not report an agent's model list, and agents pick a default on their
	 * own. But every one of these CLIs accepts a model override, so the choice is
	 * offered here as data — Key = what gets passed, Value = what the picker shows.
	 * An agent with an empty list simply shows no models, which is the honest
	 * rendering of "it decides".
	 */
	TArray<TPair<FString, FString>> Models;

	/** How the choice reaches the agent. Exactly one is used, checked in this order:
	 *    ModelEnvVar  — set in the child's environment ("ANTHROPIC_MODEL")
	 *    ModelArgFlag — appended as `<flag> <model>` ("--model")
	 *  Empty in both means the model cannot be overridden and none is offered. */
	FString ModelEnvVar;
	FString ModelArgFlag;

	/** Shown when the binary is missing — "npm i -g @anthropic-ai/claude-code". */
	FString InstallHint;

	/** Some adapters need a wrapper: `npx -y opencode-ai acp`. Purely informational,
	 *  used to explain what will be run before the user commits to it. */
	FString Notes;

	/** False for a preset we ship but cannot support on this platform. */
	bool bEnabled = true;
};

class UNREALMCPCHAT_API FMCPChatAgentCatalog
{
public:
	static FMCPChatAgentCatalog& Get();

	/** Load (or reload). User additions in Saved/ win over the shipped file, so a
	 *  plugin update never clobbers a hand-added agent. */
	void Load();

	const TArray<FMCPChatAgentInfo>& GetAgents() const { return Agents; }
	const FMCPChatAgentInfo*         FindAgent(const FString& AgentId) const;

	/** Command + args joined for CreateProc, with the MCP config flag appended when
	 *  the preset asks for it. */
	static FString BuildArgumentString(const FMCPChatAgentInfo& Agent, const FString& McpConfigPath,
	                                   const FString& ModelId = FString());

	/**
	 * Write the MCP config the agent will read.
	 *
	 * This is the highest-leverage part of the whole integration: it is what gives a
	 * local agent all 450 editor tools with no work on its side. Returns the path,
	 * or empty when the server is not running (in which case the agent is launched
	 * anyway, without editor tools, and told so).
	 */
	static FString WriteMcpConfig(const FString& AgentId);

	static FString GetAgentDirectory(const FString& AgentId);
	static FString GetUserCatalogPath();
	static FString GetShippedCatalogPath();

private:
	FMCPChatAgentCatalog() = default;
	void MergeFromFile(const FString& Path);

	TArray<FMCPChatAgentInfo> Agents;
	bool bLoaded = false;
};
